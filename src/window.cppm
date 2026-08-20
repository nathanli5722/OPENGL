module;

#include <functional>
#include <iostream>
#include <stdexcept>
#include "glad/glad.h"
#include <GLFW/glfw3.h>
#include <string>

export module window;

export class Window {
  private:
    GLFWwindow* window;
    int width, height;
    std::function<void(GLFWwindow*, float)> inputCallback;

    static void framebuffer_size_callback
                                    (GLFWwindow* window, int width, int height)
    {
        glViewport(0, 0, width, height);
    }
  public:
    Window(int t_width = 800, int t_height = 600,
           std::string name = "window")

           : width{t_width},
             height{t_height}
    {
      init(name);
    }

    void init(std::string name) {
      glfwInit();
      glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
      glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
      glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
      window = glfwCreateWindow(width, height, name.c_str(), NULL, NULL);
      if (window == NULL)
      {
          glfwTerminate();
          throw std::exception();
      }
      glfwMakeContextCurrent(window);

      if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        throw std::exception();
      glViewport(0, 0, width, height);

      glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);  
    }

    // Registers a callback invoked once per frame, before rendering, with
    // that frame's deltaTime — for polling input (glfwGetKey) and updating
    // scene state such as a Camera. Kept separate from renderLoop's lambda
    // so that one stays rendering-only.
    void setInputCallback(std::function<void(GLFWwindow*, float)> callback) {
      inputCallback = std::move(callback);
    }

    template <typename F>
    void renderLoop(F&& perloop) {
      float lastFrame = 0.0f;
      while(!glfwWindowShouldClose(window))
      {
        float currentFrame = static_cast<float>(glfwGetTime());
        float deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        if (inputCallback)
          inputCallback(window, deltaTime);

        glClear(GL_COLOR_BUFFER_BIT);

        perloop(window, deltaTime);


        // show buffer
        glfwSwapBuffers(window);
        glfwPollEvents();
      }

    }
};