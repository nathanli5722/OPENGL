module;

#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

export module shader;

// Module-private helpers, shared by both program types below.
namespace {

std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("Failed to open shader file: " + path);

    std::stringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

void checkCompileErrors(unsigned int shader, const char* label) {
    int success;
    char infoLog[1024];
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
        throw std::runtime_error(std::string("Shader compilation failed (") + label + "): " + infoLog);
    }
}

void checkLinkErrors(unsigned int prog) {
    int success;
    char infoLog[1024];
    glGetProgramiv(prog, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(prog, sizeof(infoLog), nullptr, infoLog);
        throw std::runtime_error(std::string("Shader program linking failed: ") + infoLog);
    }
}

unsigned int compile(GLenum type, const std::string& source, const char* label) {
    unsigned int shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    checkCompileErrors(shader, label);
    return shader;
}

} // namespace

// A linked GL program and its uniform interface. Shader and ComputeShader
// differ only in which stages they compile — once linked, both are just a
// program handle you bind and set uniforms on, so that part lives here.
// Copying is deleted because the destructor owns the GL program; the two
// derived types are constructed in place and referred to by reference.
export class ShaderProgram {
public:
    ~ShaderProgram() { glDeleteProgram(program); }

    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;

    void use() const {
        glUseProgram(program);
    }

    unsigned int id() const {
        return program;
    }

    void setBool(const std::string& name, bool value) const {
        glUniform1i(glGetUniformLocation(program, name.c_str()), static_cast<int>(value));
    }

    void setInt(const std::string& name, int value) const {
        glUniform1i(glGetUniformLocation(program, name.c_str()), value);
    }

    void setFloat(const std::string& name, float value) const {
        glUniform1f(glGetUniformLocation(program, name.c_str()), value);
    }

    void setMat4(const std::string& name, const glm::mat4& value) const {
        glUniformMatrix4fv(glGetUniformLocation(program, name.c_str()), 1,
                           GL_FALSE, glm::value_ptr(value));
    }

    void setVec3(const std::string& name, const glm::vec3& value) const {
        glUniform3fv(glGetUniformLocation(program, name.c_str()), 1, glm::value_ptr(value));
    }

protected:
    ShaderProgram() = default;

    unsigned int program = 0;
};

export class Shader : public ShaderProgram {
public:
    Shader(const std::string& vertexPath, const std::string& fragmentPath,
           const std::string& geometryPath = "") {
        std::string vertexCode = readFile(vertexPath);
        std::string fragmentCode = readFile(fragmentPath);

        unsigned int vertexShader = compile(GL_VERTEX_SHADER, vertexCode, "VERTEX");
        unsigned int fragmentShader = compile(GL_FRAGMENT_SHADER, fragmentCode, "FRAGMENT");
        unsigned int geometryShader = 0;
        if (!geometryPath.empty()) {
            std::string geometryCode = readFile(geometryPath);
            geometryShader = compile(GL_GEOMETRY_SHADER, geometryCode, "GEOMETRY");
        }

        program = glCreateProgram();
        glAttachShader(program, vertexShader);
        glAttachShader(program, fragmentShader);
        if (geometryShader != 0)
            glAttachShader(program, geometryShader);
        glLinkProgram(program);
        checkLinkErrors(program);

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        if (geometryShader != 0)
            glDeleteShader(geometryShader);
    }
};

// A compute program: one GL_COMPUTE_SHADER stage, no rasterization involved.
// Requires a 4.3 context, which window.cppm requests. The work it does is
// driven by dispatch() rather than by a draw call, and it communicates only
// through image/buffer bindings — so unlike Shader there is nothing to attach
// a framebuffer to, and the caller is responsible for the glMemoryBarrier
// calls that order its writes against whatever reads them next.
export class ComputeShader : public ShaderProgram {
public:
    explicit ComputeShader(const std::string& computePath) {
        std::string computeCode = readFile(computePath);
        unsigned int computeShader =
            compile(GL_COMPUTE_SHADER, computeCode, "COMPUTE");

        program = glCreateProgram();
        glAttachShader(program, computeShader);
        glLinkProgram(program);
        checkLinkErrors(program);

        glDeleteShader(computeShader);
    }

    // Raw workgroup counts — these are groups, not invocations, so the total
    // thread count is this multiplied by the shader's local_size_*.
    void dispatch(unsigned int groupsX, unsigned int groupsY,
                  unsigned int groupsZ) const {
        glDispatchCompute(groupsX, groupsY, groupsZ);
    }

    // Convenience for the common "one invocation per element" case: rounds up
    // so a size that isn't a multiple of localSize still gets full coverage.
    // The shader must then bounds-check, since the rounding spawns invocations
    // that run off the end of the data.
    static unsigned int groupCount(int size, int localSize) {
        return static_cast<unsigned int>((size + localSize - 1) / localSize);
    }
};
