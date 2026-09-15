#version 420 core

out vec4 FragColor;

in vec2 TexCoords;

// Debug view: raymarches the voxel grid directly (no cone tracing, no
// G-buffer) so you can sanity-check that voxelize.geom/.frag actually put
// geometry where you expect before trusting anything built on top of it.
uniform sampler3D voxelGrid;
uniform vec3 voxelGridBoundsMin;
uniform vec3 voxelGridBoundsMax;
uniform float voxelGridResolution;

uniform vec3 viewPos;
uniform mat4 invView;
uniform mat4 invProjection;

// Slab test: largest of the per-axis entry times is the true entry, smallest
// of the per-axis exit times is the true exit. tNear > tFar means the ray
// misses the box entirely.
vec2 intersectAABB(vec3 origin, vec3 dir, vec3 boxMin, vec3 boxMax) {
    vec3 invDir = 1.0 / dir;
    vec3 t0 = (boxMin - origin) * invDir;
    vec3 t1 = (boxMax - origin) * invDir;
    vec3 tmin = min(t0, t1);
    vec3 tmax = max(t0, t1);
    return vec2(max(max(tmin.x, tmin.y), tmin.z), min(min(tmax.x, tmax.y), tmax.z));
}

void main() {
    // Rebuild the world-space view ray for this pixel: unproject a point at
    // the far plane in clip space back through projection then view, and
    // aim from the camera at it. Same idea as reconstructing world position
    // from depth, just without an actual depth buffer here.
    vec4 clipPos = vec4(TexCoords * 2.0 - 1.0, 1.0, 1.0);
    vec4 viewSpace = invProjection * clipPos;
    viewSpace /= viewSpace.w;
    vec3 farPointWorld = (invView * viewSpace).xyz;
    vec3 rayDir = normalize(farPointWorld - viewPos);

    vec3 background = vec3(0.05, 0.05, 0.08);

    vec2 t = intersectAABB(viewPos, rayDir, voxelGridBoundsMin, voxelGridBoundsMax);
    if (t.x > t.y || t.y < 0.0) {
        FragColor = vec4(background, 1.0);
        return;
    }

    vec3 boxSize = voxelGridBoundsMax - voxelGridBoundsMin;
    float voxelSize = boxSize.x / voxelGridResolution;
    // Half a voxel per step is a reasonable default: coarse enough to be
    // "quick", fine enough not to skip over thin geometry.
    float stepSize = max(voxelSize * 0.5, 0.001);

    float tCur = max(t.x, 0.0);
    vec4 accum = vec4(0.0);

    for (int i = 0; i < 1024; i++) {
        if (tCur > t.y || accum.a > 0.99) break;

        vec3 samplePos = viewPos + rayDir * tCur;
        vec3 uvw = (samplePos - voxelGridBoundsMin) / boxSize;
        // Mip 0 explicitly: this is a "what did voxelization actually
        // write" view, not a filtered/cone-traced one.
        vec4 voxel = textureLod(voxelGrid, uvw, 0.0);

        // voxel.a is a raw accumulated fragment-weight (see VoxelGrid's
        // additive blending), not a resolved [0,1] opacity yet — clamping
        // it here is only good enough for "does this voxel look occupied,"
        // not physically-correct occlusion.
        float alpha = clamp(voxel.a, 0.0, 1.0);
        vec3 color = voxel.a > 0.0 ? voxel.rgb / voxel.a : vec3(0.0);

        accum.rgb += (1.0 - accum.a) * alpha * color;
        accum.a += (1.0 - accum.a) * alpha;

        tCur += stepSize;
    }

    vec3 finalColor = accum.rgb + background * (1.0 - accum.a);
    FragColor = vec4(finalColor, 1.0);
}
