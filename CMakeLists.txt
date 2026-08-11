cmake_minimum_required(VERSION 3.28)
project(OPENGL VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)

find_package(glfw3 REQUIRED)

add_executable(opengl_app)
target_sources(opengl_app
    PUBLIC
        FILE_SET CXX_MODULES FILES
            src/window.cppm
            
)
target_sources(opengl_app
    PRIVATE
        src/main.cpp
        src/glad.c
)

target_include_directories(opengl_app PRIVATE include)
target_link_libraries(opengl_app PRIVATE glfw)