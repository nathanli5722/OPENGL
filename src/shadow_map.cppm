module;

#include <stdexcept>
#include "glad/glad.h"

export module shadow_map;

// Owns a framebuffer with a single depth-texture attachment and no color
// attachments, for rendering a scene from a light's point of view (the
// shadow pass) and later sampling that depth in the main lighting pass to
// test whether a fragment is occluded from the light. Sized independently
// of the window/G-buffer resolution, since shadow map resolution is its own
// quality/performance knob. Kept separate from GBuffer (framebuffer.cppm)
// since the attachment layout, sampler state (border-clamped so points
// outside the light's frustum read as unshadowed), and draw-buffer setup
// are unrelated to a G-buffer's.
export class ShadowMap {
public:
    ShadowMap(int resolution) : resolution(resolution) {
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);

        glGenTextures(1, &depthTex);
        glBindTexture(GL_TEXTURE_2D, depthTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, resolution, resolution, 0,
                     GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex, 0);
        // No color attachment: tell GL this framebuffer has no draw/read
        // color target rather than leaving GL_COLOR_ATTACHMENT0 implied.
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("ShadowMap framebuffer is incomplete");

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    ~ShadowMap() {
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &depthTex);
    }

    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;

    ShadowMap(ShadowMap&& other) noexcept
        : fbo(other.fbo), depthTex(other.depthTex), resolution(other.resolution)
    {
        other.fbo = other.depthTex = 0;
    }

    ShadowMap& operator=(ShadowMap&& other) noexcept {
        if (this != &other) {
            glDeleteFramebuffers(1, &fbo);
            glDeleteTextures(1, &depthTex);

            fbo = other.fbo;
            depthTex = other.depthTex;
            resolution = other.resolution;
            other.fbo = other.depthTex = 0;
        }
        return *this;
    }

    // Shadow pass: subsequent draws (typically a depth-only shader driven
    // by the light's view/projection matrices) write into this depth
    // texture instead of the default framebuffer or G-buffer.
    void bindForWriting() const {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, resolution, resolution);
        glEnable(GL_DEPTH_TEST);
        glCullFace(GL_FRONT); //peter-panning
    }

    // Clears this shadow map's depth attachment. Call after
    // bindForWriting(), before the shadow pass's draw calls.
    void clear() const {
        glClear(GL_DEPTH_BUFFER_BIT);
    }

    // Lighting pass: binds the depth texture to the given texture unit for
    // a shadow-sampling shader to read (compare fragment depth in light
    // space against the value stored here).
    void bindForReading(unsigned int unit) const {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, depthTex);
    }

    // Returns to the default framebuffer (the window's own backbuffer).
    void unbind() const {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glCullFace(GL_BACK); // restore default culling
    }

private:
    unsigned int fbo = 0;
    unsigned int depthTex = 0;
    int resolution;
};
