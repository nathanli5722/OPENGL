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
            src/framebuffer.cppm
            src/screen_quad.cppm
            src/shadow_map.cppm
            src/light.cppm
            src/voxel_grid.cppm
)
target_sources(gl_common PRIVATE
    src/glad.c
    src/tiny_obj_loader_impl.cpp
    src/stb_image_impl.cpp
    src/tiny_gltf_impl.cpp
)
target_include_directories(gl_common PUBLIC include)
target_link_libraries(gl_common PUBLIC glfw glm::glm)
# PUBLIC (not just on tiny_gltf_impl.cpp) because tiny_gltf.h's TinyGLTF
# class inlines its default member initializers wherever it's included —
# model.cppm includes it too for declarations, and needs to see the same
# macro state as the TU that actually defines TINYGLTF_IMPLEMENTATION, or
# its inlined constructor ends up referencing symbols that were never
# compiled in (see tiny_gltf_impl.cpp for what these macros do).
target_compile_definitions(gl_common PUBLIC
    TINYGLTF_NO_STB_IMAGE
    TINYGLTF_NO_STB_IMAGE_WRITE
    TINYGLTF_NO_EXTERNAL_IMAGE
)

# Each demo lives in examples/<name>/main.cpp and becomes its own executable
# so lessons can be run and compared independently. DEMO_DIR gives each demo's
# main.cpp its own absolute directory so shader/asset paths there are just
# DEMO_DIR "/vertex.vert" — renaming a demo only means updating the folder
# name and this add_demo() call, not every path string inside main.cpp.
function(add_demo name)
    add_executable(${name} examples/${name}/main.cpp)
    target_link_libraries(${name} PRIVATE gl_common)
    target_compile_definitions(${name} PRIVATE DEMO_DIR="${CMAKE_SOURCE_DIR}/examples/${name}")
endfunction()

add_demo(1_hello_triangle)
add_demo(2_perspective)
add_demo(3_phong_lighting)
add_demo(4_model_loading)
add_demo(5_deferred_rendering)
add_demo(6_shadow_mapping)
add_demo(7_PBR)
add_demo(8_voxel_gi)
