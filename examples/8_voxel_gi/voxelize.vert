#version 420 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;

out VS_OUT {
    vec3 WorldPos;
    vec3 Normal;
    vec2 TexCoords;
} vs_out;

uniform mat4 model;

void main()
{
    vs_out.WorldPos = vec3(model * vec4(aPos, 1.0));
    vs_out.Normal = mat3(transpose(inverse(model))) * aNormal;
    vs_out.TexCoords = aTexCoords;

    // No view/projection here: voxelize.geom re-projects each triangle
    // into voxel-grid space per its own dominant axis, not from a fixed
    // camera, so gl_Position just carries WorldPos through unmodified for
    // the geometry shader to consume.
    gl_Position = vec4(vs_out.WorldPos, 1.0);
}
