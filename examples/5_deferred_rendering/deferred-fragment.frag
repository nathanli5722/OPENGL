#version 400 core
layout (location = 0) out vec3 gPosition;
layout (location = 1) out vec3 gNormal;
layout (location = 2) out vec4 gAlbedoSpec;

in vec3 WorldPos;
in vec3 Normal;
in vec2 TexCoords;


uniform vec3 albedo;
uniform bool hasAlbedoMap;
uniform sampler2D albedoMap;
uniform float roughness;

void main()
{
    vec4 texColor = hasAlbedoMap ?
                    texture(albedoMap, TexCoords) : vec4(albedo, 1.0);
    if (texColor.a < 0.9) discard; // discard transparent fragments

    gPosition = WorldPos;
    gNormal = normalize(Normal);
    // gAlbedoSpec.a carries specular strength through to the lighting pass,
    // since the diffuse alpha itself is only used for the transparency test
    // above. Smoother (lower-roughness) surfaces get a stronger highlight.
    gAlbedoSpec = vec4(texColor.rgb, 1.0 - roughness);
}
