module;

#include <stdexcept>
#include <string>
#include "glad/glad.h"
#include "stb_image.h"

export module texture;

// Whether the source image holds color data (albedo/emissive, gamma-encoded
// sRGB) or non-color data (metallic/roughness/normal maps, already linear).
// Only affects which internal format the bytes get uploaded as.
export enum class ColorSpace { Linear, SRGB };

export class Texture {
public:
    Texture(const std::string& path, ColorSpace colorSpace = ColorSpace::Linear) {
        stbi_set_flip_vertically_on_load(true);
        int width, height, channels;
        unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 0);
        if (!data)
            throw std::runtime_error("Failed to load texture: " + path);

        GLenum dataFormat = GL_RED;
        GLenum internalFormat = GL_R8;
        
        if (channels == 3) {
            dataFormat = GL_RGB;
            internalFormat = (colorSpace == ColorSpace::SRGB) ? GL_SRGB8 : GL_RGB8;
        } else if (channels == 4) {
            dataFormat = GL_RGBA;
            internalFormat = (colorSpace == ColorSpace::SRGB) ? GL_SRGB8_ALPHA8 : GL_RGBA8;
        }

        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0,
                     dataFormat, GL_UNSIGNED_BYTE, data);
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
