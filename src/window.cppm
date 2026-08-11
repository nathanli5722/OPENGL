module;

#include <iostream>
#include <stdexcept>
#include <GLFW/glfw3.h>
#include "glad/glad.h"

export module window;

export class Window {
  private:
    GLFWwindow* window;

    static void framebuffer_size_callback(GLFWwindow* window, int width, int height)
    {
        glViewport(0, 0, width, height);
    }  
  public:
    void init(int width, int height, char* name) {
      glfwInit();
      glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
      glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
      glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
      window = glfwCreateWindow(width, height, name, NULL, NULL);
      if (window == NULL)
      {
          glfwTerminate();
          throw std::exception();
      }
      glfwMakeContextCurrent(window);

      if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        throw std::exception();
      glViewport(0, 0, 800, 600);

      glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);  
    }

    void renderLoop(void(*loop)()){
      loop();
    }
};