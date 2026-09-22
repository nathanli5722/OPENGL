#version 420 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedo;
uniform sampler2D gMetallicRoughness;
layout(binding = 5) uniform sampler2D shadowMap;

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

// Voxel cone tracing indirect lighting. voxelGrid is the VoxelGrid texture
// from src/voxel_grid.cppm (mipmapped RGBA16F: rgb = averaged color, a =
// averaged opacity per voxel), bound for reading in main.cpp. Bounds/
// resolution let traceCone() convert between world space and the grid's
// [0,1]^3 texture space.
uniform sampler3D voxelGrid;
uniform vec3 voxelGridBoundsMin;
uniform vec3 voxelGridBoundsMax;
uniform float voxelGridResolution;

// TODO(you): march a cone of the given half-angle `aperture` (radians)
// from `origin` along `direction` through the voxel grid's mip chain,
// compositing radiance+opacity front-to-back (nearer samples occlude/
// contribute more than farther ones), and return the accumulated RGB
// radiance. Conceptually, at each step: track total distance traveled from
// `origin`; that distance plus `aperture` implies a cone diameter at that
// point; use the diameter (relative to a voxel's world-space size) to pick
// a mip level — wider footprint -> coarser mip, so one sample represents
// everything the cone covers there rather than one thin voxel; stop once
// accumulated opacity is near 1 or a max distance is reached.
vec3 traceCone(vec3 origin, vec3 direction, float aperture) {
    return vec3(0.0);
}

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

    // TODO(you): replace (or blend with) the flat `ambient` term above with
    // real bounce lighting: call traceCone() several times across a
    // hemisphere oriented around N (a handful of fixed directions weighted
    // toward N — the well-known 5/6-cone diffuse pattern most VCT writeups
    // use), sum the results, and combine with albedo the same way `ambient`
    // already does, e.g.:
    //   vec3 indirectDiffuse = albedo * traceHemisphere(position, N);
    //   vec3 ambient = indirectDiffuse; // instead of the flat term above

    // albedoMap/emissiveMap were uploaded as GL_SRGB8 (see texture.cppm),
    // so the GPU already linearized them on sample, and every lighting term
    // above is computed in that same linear space — correct for the math,
    // but the display expects sRGB-encoded values. Without this, the whole
    // image reads far too dark: gamma compresses dark linear values much
    // more than bright ones, so anything dim collapses toward black rather
    // than just looking dim.
    vec3 color = ambient + Lo;
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
