module;

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>
#include "glad/glad.h"
#include <glm/glm.hpp>

export module voxel_grid;

import shader;

// The grid is two representations of the same volume, in sequence.
//
// Voxelization writes into four GL_R32UI volumes — R, G, B, and a sample
// count — because a fragment shader can only atomically add to an integer
// image, and many fragments land in the same voxel. Colors go in as 8-bit
// fixed point (channel * 255) and simply sum; the count tracks how many
// fragments contributed, so the average is rgb / (255 * count).
//
// Nothing downstream can trace those directly: integer textures cannot be
// linearly filtered, and cone tracing needs trilinear samples across a mip
// chain. So a compute pass resolves the four accumulators into a single
// filterable GL_RGBA16F volume. That texture stores premultiplied radiance —
// rgb = average color, a = 1 for any occupied voxel — which is the form
// front-to-back cone accumulation expects.
//
// The coarser levels are not a mip chain of that volume. Box-filtering
// opacity turns a one-voxel wall seen face-on translucent within a few
// levels, so cones leak through walls. Instead there are six anisotropic
// volumes (+X, -X, +Y, -Y, +Z, -Z), each at half the grid resolution with its
// own full mip chain: level k of those is overall mip k + 1, built by
// voxel_mip.comp compositing along its direction before averaging. The
// isotropic volume is overall mip 0 for every direction.
export class VoxelGrid {
public:
    VoxelGrid(int resolution, glm::vec3 boundsMin, glm::vec3 boundsMax)
        : resolution(resolution), boundsMin(boundsMin), boundsMax(boundsMax),
          clearData(static_cast<std::size_t>(resolution) * resolution * resolution, 0u)
    {
        if (resolution < 2)
            throw std::runtime_error("VoxelGrid resolution must be at least 2");

        // Accumulators: level 0 only, never sampled, only bound as images.
        glGenTextures(static_cast<GLsizei>(voxelTex.size()), voxelTex.data());
        for (const auto texture : voxelTex) {
            glBindTexture(GL_TEXTURE_3D, texture);
            glTexImage3D(GL_TEXTURE_3D, 0, GL_R32UI, resolution, resolution, resolution,
                         0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        }

        // Resolved radiance: a single level — everything coarser lives in the
        // anisotropic volumes below. Linear filtering still matters for cone
        // tracing's first few samples, which read it between voxel centers.
        glGenTextures(1, &radianceTex);
        glBindTexture(GL_TEXTURE_3D, radianceTex);
        glTexStorage3D(GL_TEXTURE_3D, 1, GL_RGBA16F,
                       resolution, resolution, resolution);
        setSampledParams();

        // Anisotropic volumes: half resolution, full mip chain from there
        // down to 1^3 (the same floor-halving rule glTexStorage3D uses).
        // Trilinear filtering blends between their levels as the cone widens.
        anisoResolution = resolution / 2;
        anisoLevels = 1;
        for (int size = anisoResolution; size > 1; size >>= 1)
            ++anisoLevels;

        glGenTextures(static_cast<GLsizei>(anisoTex.size()), anisoTex.data());
        for (const auto texture : anisoTex) {
            glBindTexture(GL_TEXTURE_3D, texture);
            glTexStorage3D(GL_TEXTURE_3D, anisoLevels, GL_RGBA16F,
                           anisoResolution, anisoResolution, anisoResolution);
            setSampledParams();
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        }
        glBindTexture(GL_TEXTURE_3D, 0);

        // A framebuffer with no attachments (ARB_framebuffer_no_attachments,
        // core in 4.3). Rasterization needs *some* target to define the
        // viewport it fills, but this pass writes only through image stores,
        // so there is nothing to attach. Declaring the default width/height
        // decouples voxel resolution from the window size, which binding
        // framebuffer 0 would not do.
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferParameteri(GL_FRAMEBUFFER,
                                GL_FRAMEBUFFER_DEFAULT_WIDTH, resolution);
        glFramebufferParameteri(GL_FRAMEBUFFER,
                                GL_FRAMEBUFFER_DEFAULT_HEIGHT, resolution);
        glFramebufferParameteri(GL_FRAMEBUFFER,
                                GL_FRAMEBUFFER_DEFAULT_SAMPLES, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("VoxelGrid framebuffer is incomplete");

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    ~VoxelGrid() {
        glDeleteTextures(static_cast<GLsizei>(voxelTex.size()), voxelTex.data());
        glDeleteTextures(1, &radianceTex);
        glDeleteTextures(static_cast<GLsizei>(anisoTex.size()), anisoTex.data());
        glDeleteFramebuffers(1, &fbo);
    }

    VoxelGrid(const VoxelGrid&) = delete;
    VoxelGrid& operator=(const VoxelGrid&) = delete;

    VoxelGrid(VoxelGrid&& other) noexcept
        : voxelTex(other.voxelTex), radianceTex(other.radianceTex),
          anisoTex(other.anisoTex), fbo(other.fbo), resolution(other.resolution),
          anisoResolution(other.anisoResolution), anisoLevels(other.anisoLevels),
          boundsMin(other.boundsMin), boundsMax(other.boundsMax),
          clearData(std::move(other.clearData))
    {
        other.voxelTex.fill(0);
        other.radianceTex = 0;
        other.anisoTex.fill(0);
        other.fbo = 0;
    }

    VoxelGrid& operator=(VoxelGrid&& other) noexcept {
        if (this != &other) {
            glDeleteTextures(static_cast<GLsizei>(voxelTex.size()), voxelTex.data());
            glDeleteTextures(1, &radianceTex);
            glDeleteTextures(static_cast<GLsizei>(anisoTex.size()), anisoTex.data());
            glDeleteFramebuffers(1, &fbo);
            voxelTex = other.voxelTex;
            radianceTex = other.radianceTex;
            anisoTex = other.anisoTex;
            fbo = other.fbo;
            resolution = other.resolution;
            anisoResolution = other.anisoResolution;
            anisoLevels = other.anisoLevels;
            boundsMin = other.boundsMin;
            boundsMax = other.boundsMax;
            clearData = std::move(other.clearData);
            other.voxelTex.fill(0);
            other.radianceTex = 0;
            other.anisoTex.fill(0);
            other.fbo = 0;
        }
        return *this;
    }

    // Binds the attachment-less FBO; the shader's only output is its image
    // stores. Depth and cull are off so that every triangle rasterizes into
    // the grid regardless of facing or occlusion.
    void bindForWriting(unsigned int firstImageUnit) const {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, resolution, resolution);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        for (unsigned int i = 0; i < voxelTex.size(); ++i)
            glBindImageTexture(firstImageUnit + i, voxelTex[i], 0, GL_TRUE, 0,
                               GL_READ_WRITE, GL_R32UI);
    }

    // OpenGL 4.3 lacks glClearTexImage (introduced in 4.4), so clear level 0
    // explicitly. Static scenes pay this upload only when revoxelized.
    void clear() const {
        for (const auto texture : voxelTex) {
            glBindTexture(GL_TEXTURE_3D, texture);
            glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, resolution, resolution, resolution,
                            GL_RED_INTEGER, GL_UNSIGNED_INT, clearData.data());
        }
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    }

    // For the resolve compute pass: the four accumulators read-only on
    // firstImageUnit..+3 (matching the voxelization bindings, so the shaders
    // agree), and the radiance volume write-only on firstImageUnit + 4. The
    // barrier makes the voxelization pass's atomics visible to the loads.
    void bindForResolve(unsigned int firstImageUnit) const {
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        for (unsigned int i = 0; i < voxelTex.size(); ++i)
            glBindImageTexture(firstImageUnit + i, voxelTex[i], 0, GL_TRUE, 0,
                               GL_READ_ONLY, GL_R32UI);
        glBindImageTexture(firstImageUnit + static_cast<unsigned int>(voxelTex.size()),
                           radianceTex, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    }

    // Fills the anisotropic volumes with voxel_mip.comp, one dispatch per
    // (level, direction): level 0 of every direction reads the resolved
    // isotropic volume, each later level reads the previous level of its own
    // direction. The barrier inside the loop makes each level's image stores
    // visible to the next level's loads; the final one covers the samplers
    // that cone tracing reads through.
    void generateMips(const ComputeShader& mipShader) const {
        mipShader.use();
        for (int level = 0; level < anisoLevels; ++level) {
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
            int dstResolution = std::max(1, anisoResolution >> level);
            mipShader.setInt("dstResolution", dstResolution);
            unsigned int groups = ComputeShader::groupCount(dstResolution, 8);

            for (int dir = 0; dir < static_cast<int>(anisoTex.size()); ++dir) {
                unsigned int src = level == 0 ? radianceTex : anisoTex[dir];
                int srcLevel = level == 0 ? 0 : level - 1;
                glBindImageTexture(0, src, srcLevel, GL_TRUE, 0,
                                   GL_READ_ONLY, GL_RGBA16F);
                glBindImageTexture(1, anisoTex[dir], level, GL_TRUE, 0,
                                   GL_WRITE_ONLY, GL_RGBA16F);
                mipShader.setInt("axis", dir / 2);
                mipShader.setInt("dirSign", dir % 2 == 0 ? 1 : -1);
                mipShader.dispatch(groups, groups, groups);
            }
        }
        glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
    }

    // Binds the resolved isotropic volume to textureUnit and the six
    // anisotropic volumes, in +X, -X, +Y, -Y, +Z, -Z order, to the six units
    // after it — matching a `layout(binding = textureUnit + 1) uniform
    // sampler3D voxelAniso[6]` declaration. The debug raymarch only reads the
    // first; cone tracing reads all seven.
    void bindForReading(unsigned int textureUnit) const {
        glActiveTexture(GL_TEXTURE0 + textureUnit);
        glBindTexture(GL_TEXTURE_3D, radianceTex);
        for (unsigned int i = 0; i < anisoTex.size(); ++i) {
            glActiveTexture(GL_TEXTURE0 + textureUnit + 1 + i);
            glBindTexture(GL_TEXTURE_3D, anisoTex[i]);
        }
    }

    // Deliberately leaves the viewport at the grid's resolution. Every pass
    // that follows states its own (GBuffer::bindForWriting, Window::bindDefault)
    // — saving and restoring here would only propagate whatever the *previous*
    // pass leaked, which is how the shadow map's square 2048x2048 ended up
    // stretching the debug view.
    void unbind() const {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
    }

    unsigned int textureId() const { return radianceTex; }
    int getResolution() const { return resolution; }
    int getAnisoLevels() const { return anisoLevels; }
    glm::vec3 getBoundsMin() const { return boundsMin; }
    glm::vec3 getBoundsMax() const { return boundsMax; }

private:
    // Filtering/wrap shared by every sampled volume, applied to whichever one
    // is bound. Clamp keeps samples at the grid's edge from wrapping around
    // to the opposite wall.
    static void setSampledParams() {
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    }

    std::array<unsigned int, 4> voxelTex{};
    unsigned int radianceTex = 0;
    std::array<unsigned int, 6> anisoTex{};
    unsigned int fbo = 0;
    int resolution = 0;
    int anisoResolution = 0;
    int anisoLevels = 1;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
    std::vector<std::uint32_t> clearData;
};
