module;

#include <stdexcept>
#include "glad/glad.h"

export module framebuffer;

// Owns a framebuffer configured as a deferred-shading G-buffer: four color
// attachments (position, normal, albedo, packed metallic+roughness) plus a
// depth renderbuffer, sized to (width, height). Position and normal are
// stored as RGB16F with no alpha channel — desktop GL renders 3-component
// float formats fine as FBO attachments, so there's no need to pad them to
// RGBA just to satisfy the format. Metallic and roughness are packed
// together into one RG8 attachment (R=metallic, G=roughness) rather than
// stolen from albedo's alpha, since a PBR lighting pass always consumes
// them as a pair and this keeps each attachment's meaning self-contained.
// These attachments are allocated empty (no stb_image decode) with
// GL_CLAMP_TO_EDGE/GL_NEAREST and no mipmaps, unlike texture.cppm's
// Texture, which is for sampled surface textures loaded from disk — the
// two are intentionally separate rather than one class trying to cover
// both.
//
// Render the geometry pass into it via bindForWriting(), then unbind() and
// sample the results in a lighting pass via bindForReading(), which binds
// position/normal/albedo/metallicRoughness to texture units 0/1/2/3 (bind
// matching sampler2D uniforms to those same unit numbers).
export class GBuffer {
public:
    GBuffer(int width, int height) : width(width), height(height) {
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);

        positionTex = createAttachment(GL_RGB16F, GL_RGB, GL_FLOAT,
                                        GL_COLOR_ATTACHMENT0);
        normalTex = createAttachment(GL_RGB16F, GL_RGB, GL_FLOAT,
                                      GL_COLOR_ATTACHMENT1);

        //doesnt need alpha since deferred cant render it
        albedoTex = createAttachment(GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE,
                                      GL_COLOR_ATTACHMENT2);
        metallicRoughnessTex = createAttachment(GL_RG8, GL_RG, GL_UNSIGNED_BYTE,
                                                 GL_COLOR_ATTACHMENT3);

        unsigned int attachments[4] = {
            GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3
        };
        glDrawBuffers(4, attachments);

        glGenRenderbuffers(1, &depthRbo);
        glBindRenderbuffer(GL_RENDERBUFFER, depthRbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRbo);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("GBuffer framebuffer is incomplete");

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    ~GBuffer() {
        glDeleteFramebuffers(1, &fbo);
        glDeleteRenderbuffers(1, &depthRbo);
        glDeleteTextures(1, &positionTex);
        glDeleteTextures(1, &normalTex);
        glDeleteTextures(1, &albedoTex);
        glDeleteTextures(1, &metallicRoughnessTex);
    }

    GBuffer(const GBuffer&) = delete;
    GBuffer& operator=(const GBuffer&) = delete;

    GBuffer(GBuffer&& other) noexcept
        : fbo(other.fbo), depthRbo(other.depthRbo), positionTex(other.positionTex),
          normalTex(other.normalTex), albedoTex(other.albedoTex),
          metallicRoughnessTex(other.metallicRoughnessTex),
          width(other.width), height(other.height)
    {
        other.fbo = other.depthRbo = other.positionTex = other.normalTex
                  = other.albedoTex = other.metallicRoughnessTex = 0;
    }

    GBuffer& operator=(GBuffer&& other) noexcept {
        if (this != &other) {
            glDeleteFramebuffers(1, &fbo);
            glDeleteRenderbuffers(1, &depthRbo);
            glDeleteTextures(1, &positionTex);
            glDeleteTextures(1, &normalTex);
            glDeleteTextures(1, &albedoTex);
            glDeleteTextures(1, &metallicRoughnessTex);

            fbo = other.fbo;
            depthRbo = other.depthRbo;
            positionTex = other.positionTex;
            normalTex = other.normalTex;
            albedoTex = other.albedoTex;
            metallicRoughnessTex = other.metallicRoughnessTex;
            width = other.width;
            height = other.height;
            other.fbo = other.depthRbo = other.positionTex = other.normalTex
                      = other.albedoTex = other.metallicRoughnessTex = 0;
        }
        return *this;
    }

    // Geometry pass: subsequent draws write into this G-buffer's four
    // attachments instead of the default framebuffer.
    void bindForWriting() const {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, width, height);
        glEnable(GL_DEPTH_TEST);
    }

    // Clears this G-buffer's color and depth attachments. Call after
    // bindForWriting(), before the geometry pass's draw calls.
    void clear() const {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    // Lighting pass: binds position/normal/albedo/metallicRoughness to
    // texture units 0/1/2/3 for a full-screen-quad shader to sample.
    void bindForReading() const {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, positionTex);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, normalTex);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, albedoTex);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, metallicRoughnessTex);
    }

    // Returns to the default framebuffer (the window's own backbuffer).
    // Also turns depth testing back off since the geometry pass that
    // needed it is over; the lighting pass draws a screen-space quad with
    // no depth buffer to test against.
    void unbind() const {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDisable(GL_DEPTH_TEST);
    }

private:
    unsigned int createAttachment(GLenum internalFormat, GLenum format, GLenum type, GLenum attachment) {
        unsigned int tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, type, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, tex, 0);
        return tex;
    }

    unsigned int fbo = 0;
    unsigned int depthRbo = 0;
    unsigned int positionTex = 0, normalTex = 0, albedoTex = 0, metallicRoughnessTex = 0;
    int width, height;
};
