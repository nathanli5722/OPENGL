#version 420 core
out vec4 FragColor;

in vec2 TexCoords;

layout(binding = 0) uniform sampler2D gPosition;
layout(binding = 1) uniform sampler2D gNormal;
layout(binding = 2) uniform sampler2D gAlbedo;
layout(binding = 3) uniform sampler2D gMetallicRoughness;
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

// Voxel cone tracing indirect lighting, from src/voxel_grid.cppm's volumes
// (premultiplied RGBA16F: rgb = color * opacity, a = opacity), bound for
// reading in main.cpp. voxelGrid is the full-resolution isotropic level
// (overall mip 0); voxelAniso holds the coarser levels as six half-resolution
// directional volumes in +X, -X, +Y, -Y, +Z, -Z order, where voxelAniso[i]
// level k is overall mip k + 1 as seen by a cone travelling along that
// direction. Bounds/resolution let traceCone() convert between world space and
// the grid's [0,1]^3 texture space.
layout(binding = 6) uniform sampler3D voxelGrid;
layout(binding = 7) uniform sampler3D voxelAniso[6];
uniform vec3 voxelGridBoundsMin;
uniform vec3 voxelGridBoundsMax;
uniform float voxelGridResolution;

uniform float sceneRadius;

#define STEP_FACTOR 0.5
#define SKY_COLOR (vec3(0.6, 0.7, 0.9) * 0.2)

// Opacity/radiance of the grid at an overall mip level, as seen travelling
// along direction. Each axis picks the volume facing the cone (by the sign of
// that component) and the three are blended by the squared components, which
// sum to 1 for a unit direction. Sampler array indices must be constant (the
// per-pixel sign is not dynamically uniform), hence the ternaries.
vec4 sampleAniso(vec3 uvw, float lod, vec3 direction) {
    vec3 w = direction * direction;
    vec4 x = direction.x > 0.0 ? textureLod(voxelAniso[0], uvw, lod)
                               : textureLod(voxelAniso[1], uvw, lod);
    vec4 y = direction.y > 0.0 ? textureLod(voxelAniso[2], uvw, lod)
                               : textureLod(voxelAniso[3], uvw, lod);
    vec4 z = direction.z > 0.0 ? textureLod(voxelAniso[4], uvw, lod)
                               : textureLod(voxelAniso[5], uvw, lod);
    return w.x * x + w.y * y + w.z * z;
}

// Overall mip 0 is the isotropic volume; everything coarser is directional,
// offset by one level. Between 0 and 1 the two are blended so the switch
// does not show as a seam where cones cross one voxel in diameter.
vec4 sampleVoxels(vec3 uvw, float mip, vec3 direction) {
    if (mip >= 1.0)
        return sampleAniso(uvw, mip - 1.0, direction);
    return mix(textureLod(voxelGrid, uvw, 0.0),
               sampleAniso(uvw, 0.0, direction), mip);
}

// traces a cone, 4th component is for ao
vec4 traceCone(vec3 origin, vec3 direction, vec3 normal, float aperture) {
    float diameter;
    float voxelSize = (voxelGridBoundsMax.r - voxelGridBoundsMin.r) 
                        / voxelGridResolution;
    float dist = voxelSize;
    vec4 color = vec4(0.0), temp;
    float mip, ao = 0.0;
    float aoRange = 5.0 * voxelSize; //bigger -> more ao
    vec3 pos, uvw;
    //offset origin by 1 voxel
    origin += normal * voxelSize;

    while (color.a < .95 && dist <= sceneRadius * 2){
        diameter = max(voxelSize, 2 * tan(aperture) * dist);
        mip = log2(diameter / voxelSize);

        pos = origin + dist * direction;
        uvw = (pos - voxelGridBoundsMin) / 
                (voxelGridBoundsMax - voxelGridBoundsMin);
        if (any(lessThan(uvw, vec3(0.0))) || 
            any(greaterThan(uvw, vec3(1.0)))) 
            break;  // left the grid
        temp = sampleVoxels(uvw, mip, direction);

        color += (1.0 - color.a) * temp;

        //larger dist gets less ao
        if (dist < aoRange)
            ao += (1.0 - ao) * temp.a / (1.0 + dist / voxelSize);

        dist += diameter * STEP_FACTOR;
    }

    //color.rgb += (1.0 - color.a) * SKY_COLOR;  

    return vec4(color.rgb, ao);
}

vec4 traceHemisphere(vec3 pos, vec3 normal) {
    // any vector not parallel to N works for building the frame
    vec3 up = abs(normal.y) < 0.999 ? vec3(0, 1, 0) : vec3(1, 0, 0);

    //create tangent space
    vec3 T = normalize(cross(up, normal));
    vec3 B = cross(normal, T);

    const float aperture = radians(30.0);   // half-angle
    // tilt of side cones.
    const float theta    = radians(60.0);

    //normal facing cone
    vec4 result = 0.25 * traceCone(pos, normal, normal, aperture);

    //other 5 cones
    //translate from spherical coords
    for (int k = 0; k < 5; ++k) {
        float phi = radians(72.0) * float(k);

        //vec in tangent
        vec3 d = vec3(sin(theta) * sin(phi), cos(theta), sin(theta) * cos(phi));

        //vec in world
        vec3 dir = T * d.x + normal * d.y + B * d.z;
        result += 0.15 * traceCone(pos, dir, normal, aperture);
    }
    return result;
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

    //ambient. gi.a is the cone-weighted occlusion (0 = open, 1 = fully
    //blocked nearby), so 1 - gi.a darkens corners and crevices
    vec4 gi = traceHemisphere(position, N);
    vec3 ambient = kD * albedo * gi.rgb * (1.0 - gi.a);

    vec3 color = ambient + Lo;
    //linear to srgb
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
