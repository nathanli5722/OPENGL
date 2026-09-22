module;

#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>
#include "glad/glad.h"
#include <glm/glm.hpp>

export module voxel_grid;

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
// filterable GL_RGBA16F volume, which is what gets mipmapped and sampled.
// That texture stores premultiplied radiance — rgb = average color, a = 1
// for any occupied voxel — so that a box-filtered mip's alpha is the
// fraction of covered subvoxels and rgb is the coverage-weighted color sum,
// which is exactly the form front-to-back cone accumulation expects.
export class VoxelGrid {
public:
    VoxelGrid(int resolution, glm::vec3 boundsMin, glm::vec3 boundsMax)
        : resolution(resolution), boundsMin(boundsMin), boundsMax(boundsMax),
          clearData(static_cast<std::size_t>(resolution) * resolution * resolution, 0u)
    {
        if (resolution <= 0)
            throw std::runtime_error("VoxelGrid resolution must be positive");

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

        // Resolved radiance: the full mip chain is allocated up front by
        // glTexStorage3D so glGenerateMipmap has levels to fill, and the
        // trilinear min filter is what makes cone tracing's coarser-mip
        // samples meaningful rather than point-sampled noise.
        mipLevels = 1;
        for (int size = resolution; size > 1; size >>= 1)
            ++mipLevels;

        glGenTextures(1, &radianceTex);
        glBindTexture(GL_TEXTURE_3D, radianceTex);
        glTexStorage3D(GL_TEXTURE_3D, mipLevels, GL_RGBA16F,
                       resolution, resolution, resolution);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
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
        glDeleteFramebuffers(1, &fbo);
    }

    VoxelGrid(const VoxelGrid&) = delete;
    VoxelGrid& operator=(const VoxelGrid&) = delete;

    VoxelGrid(VoxelGrid&& other) noexcept
        : voxelTex(other.voxelTex), radianceTex(other.radianceTex),
          fbo(other.fbo), resolution(other.resolution), mipLevels(other.mipLevels),
          boundsMin(other.boundsMin), boundsMax(other.boundsMax),
          clearData(std::move(other.clearData))
    {
        other.voxelTex.fill(0);
        other.radianceTex = 0;
        other.fbo = 0;
    }

    VoxelGrid& operator=(VoxelGrid&& other) noexcept {
        if (this != &other) {
            glDeleteTextures(static_cast<GLsizei>(voxelTex.size()), voxelTex.data());
            glDeleteTextures(1, &radianceTex);
            glDeleteFramebuffers(1, &fbo);
            voxelTex = other.voxelTex;
            radianceTex = other.radianceTex;
            fbo = other.fbo;
            resolution = other.resolution;
            mipLevels = other.mipLevels;
            boundsMin = other.boundsMin;
            boundsMax = other.boundsMax;
            clearData = std::move(other.clearData);
            other.voxelTex.fill(0);
            other.radianceTex = 0;
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

    // Mips are built from the resolved RGBA16F volume, never from the integer
    // accumulators — glGenerateMipmap on an integer format is not portable and
    // the result could not be filtered anyway.
    void generateMips() const {
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        glBindTexture(GL_TEXTURE_3D, radianceTex);
        glGenerateMipmap(GL_TEXTURE_3D);
        glBindTexture(GL_TEXTURE_3D, 0);
    }

    // Binds the resolved radiance volume as a single sampler3D — this is what
    // cone tracing and the debug raymarch consume.
    void bindForReading(unsigned int textureUnit) const {
        glActiveTexture(GL_TEXTURE0 + textureUnit);
        glBindTexture(GL_TEXTURE_3D, radianceTex);
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
    int getMipLevels() const { return mipLevels; }
    glm::vec3 getBoundsMin() const { return boundsMin; }
    glm::vec3 getBoundsMax() const { return boundsMax; }

private:
    std::array<unsigned int, 4> voxelTex{};
    unsigned int radianceTex = 0;
    unsigned int fbo = 0;
    int resolution = 0;
    int mipLevels = 1;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
    std::vector<std::uint32_t> clearData;
};
