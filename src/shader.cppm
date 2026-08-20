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

export class Shader {
public:
    Shader(const std::string& vertexPath, const std::string& fragmentPath) {
        std::string vertexCode = readFile(vertexPath);
        std::string fragmentCode = readFile(fragmentPath);

        unsigned int vertexShader = compile(GL_VERTEX_SHADER, vertexCode, "VERTEX");
        unsigned int fragmentShader = compile(GL_FRAGMENT_SHADER, fragmentCode, "FRAGMENT");

        program = glCreateProgram();
        glAttachShader(program, vertexShader);
        glAttachShader(program, fragmentShader);
        glLinkProgram(program);
        checkLinkErrors(program);

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
    }

    ~Shader() {
        glDeleteProgram(program);
    }

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

private:
    unsigned int program = 0;

    static std::string readFile(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open())
            throw std::runtime_error("Failed to open shader file: " + path);

        std::stringstream stream;
        stream << file.rdbuf();
        return stream.str();
    }

    static unsigned int compile(GLenum type, const std::string& source, const char* label) {
        unsigned int shader = glCreateShader(type);
        const char* src = source.c_str();
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);
        checkCompileErrors(shader, label);
        return shader;
    }

    static void checkCompileErrors(unsigned int shader, const char* label) {
        int success;
        char infoLog[1024];
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
            throw std::runtime_error(std::string("Shader compilation failed (") + label + "): " + infoLog);
        }
    }

    static void checkLinkErrors(unsigned int prog) {
        int success;
        char infoLog[1024];
        glGetProgramiv(prog, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(prog, sizeof(infoLog), nullptr, infoLog);
            throw std::runtime_error(std::string("Shader program linking failed: ") + infoLog);
        }
    }
};
