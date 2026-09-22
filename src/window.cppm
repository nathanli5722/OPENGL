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

    // Keeps the stored pixel size in sync with the real framebuffer, so that
    // bindDefault() below stays correct across resizes.
    static void framebuffer_size_callback
                                    (GLFWwindow* window, int width, int height)
    {
        glViewport(0, 0, width, height);
        if (auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window))) {
            self->width = width;
            self->height = height;
        }
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
      glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
      glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
      glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
      // width/height are the framebuffer size the caller wants, in pixels.
      // glfwCreateWindow takes screen coordinates, which a HiDPI compositor
      // multiplies by a content scale — so passing 1920x1080 straight through
      // yields 1920*scale pixels, and at 1.8x that is 3456x1944: larger than a
      // 2256x1504 panel, with most of the window off-screen and the renderer
      // shading pixels nothing can display. Dividing by the scale up front
      // makes the request mean the same thing on any display, rather than
      // needing a different constant per machine.
      float scaleX = 1.0f, scaleY = 1.0f;
      if (GLFWmonitor* monitor = glfwGetPrimaryMonitor())
        glfwGetMonitorContentScale(monitor, &scaleX, &scaleY);
      if (scaleX <= 0.0f) scaleX = 1.0f;
      if (scaleY <= 0.0f) scaleY = 1.0f;

      window = glfwCreateWindow(static_cast<int>(width / scaleX),
                                static_cast<int>(height / scaleY),
                                name.c_str(), NULL, NULL);
      if (window == NULL)
      {
          glfwTerminate();
          throw std::exception();
      }
      glfwMakeContextCurrent(window);

      if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress))
        throw std::exception();

      // glfwCreateWindow takes a size in screen coordinates, which on a HiDPI
      // display is not the size in pixels — a 1920x1080 request can come back
      // as a 3456x1944 framebuffer. glViewport works in pixels, and the resize
      // callback only fires on an actual resize (not on the scale applied at
      // creation), so querying once here is what makes the first frame cover
      // the whole window instead of a corner of it.
      glfwGetFramebufferSize(window, &width, &height);
      glViewport(0, 0, width, height);
      glEnable(GL_DEPTH_TEST);

      glfwSetWindowUserPointer(window, this);
      glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    }

    // Returns to the window's own framebuffer *and* sets the viewport to cover
    // it. The viewport is global GL state that every offscreen pass overwrites
    // with its own target's size and none of them restore, so any pass drawing
    // to the default framebuffer has to state its own — otherwise it inherits
    // whatever the previous pass left (e.g. the shadow map's square 2048x2048,
    // which stretches a 16:9 projection vertically).
    void bindDefault() const {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glViewport(0, 0, width, height);

    }

    // Framebuffer size in pixels, which on a HiDPI display is not the size
    // the window was requested at.
    int getWidth() const { return width; }
    int getHeight() const { return height; }
    float getAspect() const {
      return static_cast<float>(width) / static_cast<float>(height);
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

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        perloop(window, deltaTime);


        // show buffer
        glfwSwapBuffers(window);
        glfwPollEvents();
      }

    }
};