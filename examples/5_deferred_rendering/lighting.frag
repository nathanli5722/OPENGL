#version 400 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedoSpec;

uniform vec3 viewPos;

struct DirLight {
    vec3 direction;
    vec3 color;
    float intensity;
};

uniform DirLight dirLight;
uniform vec3 ambientColor;
uniform float ambientIntensity;
uniform float shininess;

void main()
{
    vec3 position = texture(gPosition, TexCoords).rgb;
    vec3 normal = normalize(texture(gNormal, TexCoords).rgb);
    vec4 albedoSpec = texture(gAlbedoSpec, TexCoords);
    vec3 albedo = albedoSpec.rgb;
    float specularStrength = albedoSpec.a;

    float diff = max(dot(normal, dirLight.direction), 0.0);

    vec3 viewDir = normalize(viewPos - position);
    vec3 halfDir = normalize(dirLight.direction + viewDir);

    // Gate specular on diff so a surface facing away from the light can't
    // still pick up a highlight from a grazing half-vector angle.
    float specAngle = diff > 0.0 ? max(dot(halfDir, normal), 0.0) : 0.0;
    float spec = pow(specAngle, shininess);

    vec3 ambient = albedo * ambientColor * ambientIntensity;
    // Diffuse reflects the surface's own color; specular reflects the
    // light's color (not the albedo) scaled by the surface's specular
    // strength, matching how a dielectric highlight looks.
    vec3 diffuse = albedo * dirLight.color * diff * dirLight.intensity;
    vec3 specular = dirLight.color * specularStrength * spec * dirLight.intensity;

    FragColor = vec4(ambient + diffuse + specular, 1.0);
}
