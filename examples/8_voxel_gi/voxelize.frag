#version 420 core

out vec4 FragColor;

in vec3 WorldPos;
in vec3 Normal;
in vec2 TexCoords;

flat in int mainAxis;

uniform int voxelResolution;

// Only the color channel is scaffolded here — a first-pass diffuse-only
// voxel grid only needs albedo. Add emissive/metallic/roughness uniforms
// (mirroring deferred-fragment.frag's material block) later if you want
// emissive surfaces to inject light or a richer GI response.

uniform vec3 albedo;
uniform bool hasAlbedoMap;
uniform float metallic;
uniform bool hasMetallicMap;

struct DirLight {
    vec3 direction;
    vec3 color;
    float intensity;
};

uniform mat4 lightSpaceMatrix;
uniform DirLight dirLight;

//model::draw  uses 0-4
layout(binding = 0) uniform sampler2D albedoMap;
layout(binding = 2) uniform sampler2D metallicMap;
layout(binding = 5) uniform sampler2D shadowMap;

//image units separate from texture units
layout(binding = 1, r32ui) uniform coherent uimage3D rVoxel;
layout(binding = 2, r32ui) uniform coherent uimage3D gVoxel;
layout(binding = 3, r32ui) uniform coherent uimage3D bVoxel;
layout(binding = 4, r32ui) uniform coherent uimage3D aVoxel;

float calcShadow();

//gets the voxel the fragment is in. gl_FragCoord.xy is already in window
//coordinates — the viewport is voxelResolution wide and tall during this
//pass, so it needs no remapping, just a truncation to the texel index.
//gl_FragCoord.z is the [0, 1] position along the dominant axis (see
//voxelize.geom), so it scales to the slice index.
ivec3 getVoxelPos() {
  ivec3 voxelPos;
  ivec2 otherPos = ivec2(gl_FragCoord.xy);
  int layer = clamp(int(gl_FragCoord.z * voxelResolution), 0, voxelResolution - 1);
  if (mainAxis == 0)
    voxelPos = ivec3(layer, otherPos);
  else if (mainAxis == 1)
    voxelPos = ivec3(otherPos.x, layer, otherPos.y);
  else
    voxelPos = ivec3(otherPos, layer);

  return voxelPos;
}

#define PI 3.14159
#define bias 0.01

void main()
{
  vec3 color = hasAlbedoMap ? vec3(texture(albedoMap, TexCoords)) : albedo;
  // Only sample when the mesh actually has a map: otherwise unit 2 still holds
  // whatever was bound last (last frame's G-buffer albedo), not a metallic map.
  float metal = hasMetallicMap ? texture(metallicMap, TexCoords).r : metallic;

  float kD = 1.0 - metal; //no view vector
  vec3 lambert = color / PI;

  float lit = 1.0 - calcShadow();

  vec3 result = kD * lambert * 
        dirLight.color * dirLight.intensity * 
        lit * max(dot(normalize(Normal), dirLight.direction), 0.0);

  //not necessary
  FragColor = vec4(result, 1.0);


  ivec3 fragVoxelPos = getVoxelPos();

  // 8-bit fixed point per channel, summed across every fragment that lands in
  // this voxel; aVoxel counts those fragments. A consumer recovers the average
  // color as rgb / (255 * count) — see the resolve pass.
  uvec3 fixedColor = uvec3(clamp(result, 0.0, 1.0) * 255.0);
  //uvec3 fixedColor = uvec3(color * 255.0);

  imageAtomicAdd(rVoxel, fragVoxelPos, fixedColor.r);
  imageAtomicAdd(gVoxel, fragVoxelPos, fixedColor.g);
  imageAtomicAdd(bVoxel, fragVoxelPos, fixedColor.b);
  imageAtomicAdd(aVoxel, fragVoxelPos, 1u);
}

float calcShadow() {
    vec4 lightSpacePos = lightSpaceMatrix * vec4(WorldPos, 1.0);
    vec3 afterProjection = lightSpacePos.xyz / lightSpacePos.w;
    afterProjection = afterProjection * 0.5 + 0.5;

    float shadow = texture(shadowMap, afterProjection.xy).r;

    return (afterProjection.z > shadow + bias) ? 1.0 : 0.0;
}
