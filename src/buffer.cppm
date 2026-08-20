module;

#include <cstddef>
#include <vector>
#include "glad/glad.h"

export module buffer;

export struct VertexData {
    float x, y, z;
};

// Describes one vertex attribute within a vertex struct T: `components`
// floats starting at byte `offset` (typically offsetof(T, field)), bound to
// shader `location`. Pass one of these per attribute the vertex struct
// carries (position, normal, texcoord, ...) when constructing VertexBuffer.
export struct VertexAttribute {
    unsigned int location;
    int components;
    std::size_t offset;
};

export template <typename T>
class VertexBuffer {
public:
    VertexBuffer(std::vector<T>& vertices, std::vector<unsigned int>& indices,
                 const std::vector<VertexAttribute>& layout)
    {
        vertex_count = static_cast<unsigned int>(indices.size());
        // Create and bind VAO
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);

        // Create and bind VBO
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(T), vertices.data(), GL_STATIC_DRAW);

        // Create and bind EBO
        glGenBuffers(1, &ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

        // Set vertex attribute pointers as described by the caller's layout
        for (const auto& attr : layout) {
            glEnableVertexAttribArray(attr.location);
            glVertexAttribPointer(attr.location, attr.components, GL_FLOAT, GL_FALSE,
                                   sizeof(T), reinterpret_cast<void*>(attr.offset));
        }

        // Unbind
        glBindVertexArray(0);
    }

    ~VertexBuffer() {
        glDeleteVertexArrays(1, &vao);
        glDeleteBuffers(1, &vbo);
        glDeleteBuffers(1, &ebo);
    }

    VertexBuffer(const VertexBuffer&) = delete;
    VertexBuffer& operator=(const VertexBuffer&) = delete;

    VertexBuffer(VertexBuffer&& other) noexcept
        : vao(other.vao), vbo(other.vbo), ebo(other.ebo), vertex_count(other.vertex_count)
    {
        other.vao = other.vbo = other.ebo = 0;
        other.vertex_count = 0;
    }

    VertexBuffer& operator=(VertexBuffer&& other) noexcept {
        if (this != &other) {
            glDeleteVertexArrays(1, &vao);
            glDeleteBuffers(1, &vbo);
            glDeleteBuffers(1, &ebo);

            vao = other.vao;
            vbo = other.vbo;
            ebo = other.ebo;
            vertex_count = other.vertex_count;
            other.vao = other.vbo = other.ebo = 0;
            other.vertex_count = 0;
        }
        return *this;
    }

    void bind() const {
        glBindVertexArray(vao);
    }

    void unbind() const {
        glBindVertexArray(0);
    }

    void draw() const {
        glDrawElements(GL_TRIANGLES, vertex_count, GL_UNSIGNED_INT, 0);
    }

private:
    unsigned int vao = 0, vbo = 0, ebo = 0;
    unsigned int vertex_count = 0;
};
