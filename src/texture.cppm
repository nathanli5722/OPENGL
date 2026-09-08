module;

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>
#include "glad/glad.h"
#include "stb_image.h"

export module texture;

// Whether the source image holds color data (albedo/emissive, gamma-encoded
// sRGB) or non-color data (metallic/roughness/normal maps, already linear).
// Only affects which internal format the bytes get uploaded as.
export enum class ColorSpace { Linear, SRGB };

export class Texture {
public:
    // `channel` selects a single source channel (0=R, 1=G, 2=B, 3=A) to
    // upload as a standalone GL_R8 texture instead of the image's full
    // channel count — for a source that packs multiple independent maps
    // into one file, e.g. glTF's metallic-roughness texture (G=roughness,
    // B=metallic). -1 (the default) keeps every channel the file has, which
    // is what every other map (albedo, normal, emissive, or an OBJ's own
    // single-channel roughness/metallic map) wants.
    Texture(const std::string& path, ColorSpace colorSpace = ColorSpace::Linear, int channel = -1) {
        stbi_set_flip_vertically_on_load(true);
        int width, height, channels;
        unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 0);
        if (!data)
            throw std::runtime_error("Failed to load texture: " + path);

        if (channel >= 0 && channel >= channels) {
            stbi_image_free(data);
            throw std::runtime_error("Texture: requested channel " + std::to_string(channel) +
                                      " but " + path + " only has " + std::to_string(channels) + " channel(s)");
        }

        GLenum dataFormat = GL_RED;
        GLenum internalFormat = GL_R8;
        unsigned char* uploadData = data;
        std::vector<unsigned char> extracted;

        if (channel >= 0) {
            // channels > 1: pick out just the requested channel, tightly
            // packed, so it uploads as a single-channel texture like any
            // other grayscale map.
            extracted.resize(static_cast<std::size_t>(width) * height);
            for (int i = 0; i < width * height; ++i)
                extracted[i] = data[i * channels + channel];
            uploadData = extracted.data();
        } else if (channels == 3) {
            dataFormat = GL_RGB;
            internalFormat = (colorSpace == ColorSpace::SRGB) ? GL_SRGB8 : GL_RGB8;
        } else if (channels == 4) {
            dataFormat = GL_RGBA;
            internalFormat = (colorSpace == ColorSpace::SRGB) ? GL_SRGB8_ALPHA8 : GL_RGBA8;
        }

        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0,
                     dataFormat, GL_UNSIGNED_BYTE, uploadData);
        glGenerateMipmap(GL_TEXTURE_2D);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        stbi_image_free(data);
    }

    ~Texture() {
        glDeleteTextures(1, &id);
    }


    //prevent copy/move behavior issues
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    Texture(Texture&& other) noexcept : id(other.id) {
        other.id = 0;
    }

    Texture& operator=(Texture&& other) noexcept {
        if (this != &other) {
            glDeleteTextures(1, &id);
            id = other.id;
            other.id = 0;
        }
        return *this;
    }

    void bind(unsigned int unit = 0) const {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, id);
    }

    unsigned int id = 0;
};
