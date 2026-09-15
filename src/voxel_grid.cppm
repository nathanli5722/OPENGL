module;

#include <stdexcept>
#include "glad/glad.h"
#include <glm/glm.hpp>

export module voxel_grid;

// Owns a single GL_TEXTURE_3D (resolution^3, RGBA16F) used as a voxelized
// radiance/opacity grid for voxel cone tracing: rgb accumulates
// color*weight and a accumulates weight during the voxelization pass, so
// additive blending (GL_ONE, GL_ONE) onto this texture reads back as a
// weighted average per voxel — no imageAtomic*/image-load-store needed for
// this first pass, just plain rasterization + blending. boundsMin/boundsMax
// is the world-space AABB the grid covers; the voxelization and
// cone-tracing shaders are responsible for mapping world positions into and
// out of that box (this class only owns the texture's lifecycle, not that
// mapping).
//
// The write target is attached via plain glFramebufferTexture (not
// glFramebufferTexture3D, which pins one Z-slice) so it's a *layered*
// attachment: a geometry shader can route each primitive to an arbitrary
// Z-layer via gl_Layer, which is how single-pass dominant-axis voxelization
// writes into the whole volume from one draw instead of three orthogonal
// passes.
//
// Same bindForWriting()/clear()/bindForReading()/unbind() + RAII + move-only
// shape as GBuffer (framebuffer.cppm) and ShadowMap (shadow_map.cppm).
export class VoxelGrid {
public:
    VoxelGrid(int resolution, glm::vec3 boundsMin, glm::vec3 boundsMax)
        : resolution(resolution), boundsMin(boundsMin), boundsMax(boundsMax)
    {
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);

        glGenTextures(1, &voxelTex);
        glBindTexture(GL_TEXTURE_3D, voxelTex);
        glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA16F, resolution, resolution, resolution,
                     0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        // Layered attachment (no zoffset): lets a geometry shader select
        // gl_Layer per-primitive rather than binding one fixed Z-slice.
        glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, voxelTex, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glReadBuffer(GL_NONE);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("VoxelGrid framebuffer is incomplete");

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    ~VoxelGrid() {
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &voxelTex);
    }

    VoxelGrid(const VoxelGrid&) = delete;
    VoxelGrid& operator=(const VoxelGrid&) = delete;

    VoxelGrid(VoxelGrid&& other) noexcept
        : fbo(other.fbo), voxelTex(other.voxelTex), resolution(other.resolution),
          boundsMin(other.boundsMin), boundsMax(other.boundsMax)
    {
        other.fbo = other.voxelTex = 0;
    }

    VoxelGrid& operator=(VoxelGrid&& other) noexcept {
        if (this != &other) {
            glDeleteFramebuffers(1, &fbo);
            glDeleteTextures(1, &voxelTex);

            fbo = other.fbo;
            voxelTex = other.voxelTex;
            resolution = other.resolution;
            boundsMin = other.boundsMin;
            boundsMax = other.boundsMax;
            other.fbo = other.voxelTex = 0;
        }
        return *this;
    }

    // Voxelization pass: subsequent draws (typically a vertex/geometry/
    // fragment trio that rasterizes the scene into grid space and writes
    // gl_Layer per-primitive) write into this texture instead of the
    // default framebuffer. Depth testing and face culling are off since
    // every triangle must contribute regardless of winding or occlusion —
    // there is no camera/depth concept during voxelization, only additive
    // blending of whatever writes land in each voxel.
    void bindForWriting() const {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, resolution, resolution);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
    }

    // Clears every layer of the voxel texture. Call after bindForWriting(),
    // before the voxelization pass's draw calls. (glClear on a layered
    // color attachment clears every layer in one call, no per-layer loop
    // needed.)
    void clear() const {
        glClear(GL_COLOR_BUFFER_BIT);
    }

    // Rebuilds the mip chain from the base level after voxelization, via
    // GL's default (isotropic, box-filter) mipmap generation. This is only
    // an approximation for cone tracing — a direction-aware chain would
    // need six separately-filtered mip chains (±X/±Y/±Z) to avoid light
    // leaking through thin geometry — but it's a reasonable starting point.
    void generateMips() const {
        glBindTexture(GL_TEXTURE_3D, voxelTex);
        glGenerateMipmap(GL_TEXTURE_3D);
    }

    // Cone-tracing read pass: binds the voxel texture as a sampler3D on the
    // given unit for a shader to sample (typically via textureLod, picking
    // a mip level from the cone's footprint at each step).
    void bindForReading(unsigned int unit) const {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_3D, voxelTex);
    }

    // Returns to the default framebuffer (the window's own backbuffer) and
    // restores the state bindForWriting() changed.
    void unbind() const {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glDisable(GL_BLEND);
    }

    unsigned int textureId() const { return voxelTex; }
    int getResolution() const { return resolution; }
    glm::vec3 getBoundsMin() const { return boundsMin; }
    glm::vec3 getBoundsMax() const { return boundsMax; }

private:
    unsigned int fbo = 0;
    unsigned int voxelTex = 0;
    int resolution = 0;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
};
