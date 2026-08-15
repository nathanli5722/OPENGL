module;

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
    void (*processInput)(GLFWwindow *window);

    static void framebuffer_size_callback
                                    (GLFWwindow* window, int width, int height)
    {
        glViewport(0, 0, width, height);
    }  
  public:
    Window(void (*pInput)(GLFWwindow *window) = nullptr,
           int t_width = 800, int t_height = 600, 
           std::string name = "window") 

           : processInput{pInput}, 
             width{t_width}, 
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

    void renderLoop(void(*perloop)(GLFWwindow* window)) {
      while(!glfwWindowShouldClose(window))
      {
        // input
        if (processInput)
          processInput(window);

        glClear(GL_COLOR_BUFFER_BIT);

        perloop(window);


        // show buffer
        glfwSwapBuffers(window);
        glfwPollEvents();    
      }
      
    }
};