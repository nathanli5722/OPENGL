#version 400 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedo;
uniform sampler2D gMetallicRoughness;
//no emmissive map, so shadow is 4
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

float calcShadowBlur(int x, int y, vec2 texelSize);

//bias should be uniform
#define bias 0.01
#define PI 3.14159

//fuctions from https://learnopengl.com/PBR/Lighting
float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness*roughness;
    float a2 = a*a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;

    float nom   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}
// ----------------------------------------------------------------------------
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float nom   = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}
// ----------------------------------------------------------------------------
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}
// ----------------------------------------------------------------------------
vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main()
{
    vec3 position = texture(gPosition, TexCoords).rgb;
    vec3 N = normalize(texture(gNormal, TexCoords).rgb);
    vec3 albedo = texture(gAlbedo, TexCoords).rgb;
    vec2 metallicRoughness = texture(gMetallicRoughness, TexCoords).rg;
    float metallic = metallicRoughness.r;
    // Clamped off zero: DistributionGGX's denominator collapses toward zero
    // as roughness does, turning an exact 0.0 texel (a mirror-finish pixel
    // in a source texture, or a material with no roughness data at all)
    // into a blown-out or NaN highlight instead of a normal, if very sharp,
    // specular response.
    float roughness = max(metallicRoughness.g, 0.025);

    vec3 V = normalize(viewPos - position);
    vec3 L = dirLight.direction;
    vec3 H = normalize(V + L);

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

    // from https://learnopengl.com/PBR/Lighting
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;

    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    float NdotL = max(dot(N, L), 0.0);
    vec3 radiance = dirLight.color * dirLight.intensity * lit;
    vec3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;

    // No irradiance map yet, so ambient stays this project's existing flat
    // approximation of indirect light rather than a real IBL term.
    vec3 ambient = albedo * ambientColor * ambientIntensity;

    // albedoMap/emissiveMap were uploaded as GL_SRGB8 (see texture.cppm),
    // so the GPU already linearized them on sample, and every lighting term
    // above is computed in that same linear space — correct for the math,
    // but the display expects sRGB-encoded values. Without this, the whole
    // image reads far too dark: gamma compresses dark linear values much
    // more than bright ones, so anything dim collapses toward black rather
    // than just looking dim.
    vec3 color = ambient + Lo;
    // ACES filmic tone map (Narkowicz's fit): the sun is brighter than 1.0
    // (see main.cpp), so compress HDR values into 0..1 before gamma instead
    // of clipping them. Chosen over Reinhard, whose curve lifts shadows and
    // flattens contrast into a washed-out grey; ACES's toe keeps darks dark.
    color = clamp((color * (2.51 * color + 0.03)) /
                  (color * (2.43 * color + 0.59) + 0.14), 0.0, 1.0);
    color = pow(color, vec3(1.0 / 2.2));

    FragColor = vec4(color, 1.0);
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