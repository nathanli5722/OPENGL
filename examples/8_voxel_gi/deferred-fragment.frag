#version 400 core
layout (location = 0) out vec3 gPosition;
layout (location = 1) out vec3 gNormal;
layout (location = 2) out vec3 gAlbedo;
layout (location = 3) out vec2 gMetallicRoughness;

in vec3 WorldPos;
in vec3 Normal;
in vec2 TexCoords;


uniform vec3 albedo;
uniform bool hasAlbedoMap;
uniform sampler2D albedoMap;
uniform float metallic;
uniform bool hasMetallicMap;
uniform sampler2D metallicMap;
uniform float roughness;
uniform bool hasRoughnessMap;
uniform sampler2D roughnessMap;

void main()
{
    vec4 texColor = hasAlbedoMap ?
                    texture(albedoMap, TexCoords) : vec4(albedo, 1.0);
    if (texColor.a < 0.9) discard; // discard transparent fragments

    gPosition = WorldPos;
    gNormal = normalize(Normal);
    gAlbedo = texColor.rgb;
    gMetallicRoughness = vec2(
        hasMetallicMap ? texture(metallicMap, TexCoords).r : metallic,
        hasRoughnessMap ? texture(roughnessMap, TexCoords).r : roughness
    );
}
