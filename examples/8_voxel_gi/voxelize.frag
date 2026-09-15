#version 420 core

out vec4 FragColor;

in vec3 WorldPos;
in vec3 Normal;
in vec2 TexCoords;

// Only the color channel is scaffolded here — a first-pass diffuse-only
// voxel grid only needs albedo. Add emissive/metallic/roughness uniforms
// (mirroring deferred-fragment.frag's material block) later if you want
// emissive surfaces to inject light or a richer GI response.
uniform vec3 albedo;
uniform bool hasAlbedoMap;
uniform sampler2D albedoMap;

// bindForWriting() enables additive blending (GL_ONE, GL_ONE), so every
// fragment that lands in a given voxel accumulates color into rgb and
// weight into alpha — a plain vec4(color, 1.0) here is enough to get a
// weighted average back once you read the grid, but confirm that in the
// Stage A debug visualizer before trusting it.
void main()
{
  vec3 color = hasAlbedoMap ? vec3(texture(albedoMap, TexCoords)) : albedo;
  FragColor = vec4(color, 1.0);
}
