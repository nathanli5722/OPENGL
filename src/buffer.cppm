module;

#include <vector>
#include <glad/glad.h>

export module buffer;

export struct VertexData {
    float x, y, z;
    // We can add more attributes here later, 
    // but for now we assume x, y, z are the first elements.
};

export class VertexBuffer {
public:
    VertexBuffer(std::vector<VertexData>& vertices, std::vector<unsigned int>& indices)
        {
        vertex_count = static_cast<unsigned int>(indices.size());
        // Create and bind VAO
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);

        // Create and bind VBO
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(VertexData), vertices.data(), GL_STATIC_DRAW);

        // Create and bind EBO
        glGenBuffers(1, &ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

        // Set vertex attribute pointers
        // Assuming Vertex starts with float x, y, z
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(VertexData), (void*)0);

        // Unbind
        glBindVertexArray(0);
    }

    ~VertexBuffer() {
        glDeleteVertexArrays(1, &vao);
        glDeleteBuffers(1, &vbo);
        glDeleteBuffers(1, &ebo);
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
    unsigned int vao, vbo, ebo;
    unsigned int vertex_count;
};