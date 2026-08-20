cmake_minimum_required(VERSION 3.28)
project(OPENGL VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Lets demo executables load shader files by an absolute path so they work
# no matter what directory they're launched from (e.g. from build/).
add_compile_definitions(PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

find_package(glfw3 REQUIRED)

include(FetchContent)
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
)
FetchContent_MakeAvailable(glm)

# Modules + glad shared by every demo executable below.
add_library(gl_common STATIC)
target_sources(gl_common
    PUBLIC
        FILE_SET CXX_MODULES FILES
            src/window.cppm
            src/buffer.cppm
            src/shader.cppm
            src/camera.cppm
            src/model.cppm
            src/texture.cppm
)
target_sources(gl_common PRIVATE
    src/glad.c
    src/tiny_obj_loader_impl.cpp
    src/stb_image_impl.cpp
)
target_include_directories(gl_common PUBLIC include)
target_link_libraries(gl_common PUBLIC glfw glm::glm)

# Each demo lives in examples/<name>/main.cpp and becomes its own executable
# so lessons can be run and compared independently.
function(add_demo name)
    add_executable(${name} examples/${name}/main.cpp)
    target_link_libraries(${name} PRIVATE gl_common)
endfunction()

add_demo(1_hello_triangle)
add_demo(2_perspective)
add_demo(3_phong_lighting)
add_demo(4_model_loading)
