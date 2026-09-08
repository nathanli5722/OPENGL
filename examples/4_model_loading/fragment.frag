#version 400 core

out vec4 FragColor;

in vec3 WorldPos;
in vec3 Normal;
in vec2 TexCoords;

uniform vec3 lightPositions[1];
uniform vec3 lightColors[1];

uniform vec3 albedo;
uniform bool hasAlbedoMap;
uniform sampler2D albedoMap;

void main()
{
    vec3 baseColor = hasAlbedoMap ? texture(albedoMap, TexCoords).rgb : albedo;

    vec3 N = normalize(Normal);
    vec3 L = normalize(lightPositions[0] - WorldPos);
    float diff = max(dot(N, L), 0.0);

    vec3 ambient = 0.1 * baseColor;
    vec3 diffuse = diff * lightColors[0] * baseColor;

    FragColor = vec4(ambient + diffuse, 1.0);
}
