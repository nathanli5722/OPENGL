#version 400 core

layout (location = 0) in vec3 aPos;

// No need to output normal or texcoords for shadow map

uniform mat4 lightSpaceMatrix;
uniform mat4 model;

void main()
{
    gl_Position = lightSpaceMatrix * model * vec4(aPos, 1.0);
}
