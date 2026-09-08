#version 400 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedoSpec;
uniform sampler2D shadowMap;

uniform vec3 viewPos;

struct DirLight {
    vec3 direction;
    vec3 color;
    float intensity;
};

uniform mat4 lightSpaceMatrix;
uniform DirLight dirLight;
uniform vec3 ambientColor;
uniform float ambientIntensity;
uniform float shininess;

float calcShadow();
float calcShadowBlur(int x, int y, vec2 texelSize);

//bias should be uniform
#define bias 0.01

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

    float inShadow = 0.0f;
    //pcf -> box blur
    vec2 texelSize = vec2(1.0) / textureSize(shadowMap, 0);
    for (int i = -1; i <= 1; ++i) {
        for (int j = -1; j <= 1; ++j)
        {
            inShadow += calcShadowBlur(i, j, texelSize);
        }
    }
    inShadow /= 9.0;
    float lit = 1.0 - inShadow;
    vec3 ambient = albedo * ambientColor * ambientIntensity;
    // Diffuse reflects the surface's own color; specular reflects the
    // light's color (not the albedo) scaled by the surface's specular
    // strength, matching how a dielectric highlight looks. Both are zeroed
    // by shadowed fragments; ambient is not, since ambient approximates
    // indirect light that isn't blocked by this one occluder test.
    vec3 diffuse = albedo * dirLight.color * diff * dirLight.intensity * lit;
    vec3 specular = dirLight.color * specularStrength * spec * dirLight.intensity * lit;
    

    FragColor = vec4(ambient + diffuse + specular, 1.0);
}

float calcShadow() {
    vec4 lightSpacePos = lightSpaceMatrix * vec4(texture(gPosition, TexCoords).rgb, 1.0);
    vec3 afterProjection = lightSpacePos.xyz / lightSpacePos.w;
    afterProjection = afterProjection * 0.5 + 0.5;
    
    float shadow = texture(shadowMap, afterProjection.xy).r;

    return (afterProjection.z > shadow + bias) ? 1.0 : 0.0;
}

float calcShadowBlur(int x, int y, vec2 texelSize) {
    vec4 lightSpacePos = lightSpaceMatrix * vec4(texture(gPosition, TexCoords).rgb, 1.0);
    vec3 afterProjection = lightSpacePos.xyz / lightSpacePos.w;
    afterProjection = afterProjection * 0.5 + 0.5;
    
    float shadow = texture(shadowMap, afterProjection.xy + vec2(x, y) * texelSize).r;

    return (afterProjection.z > shadow + bias) ? 1.0 : 0.0;
}