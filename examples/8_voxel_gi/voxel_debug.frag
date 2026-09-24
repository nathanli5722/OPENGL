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

    // Grid traversal (Amanatides & Woo, "A Fast Voxel Traversal Algorithm")
    // instead of fixed-size steps: the ray walks cell to cell, always crossing
    // whichever boundary is nearest, so it visits every voxel it passes
    // through exactly once — no skipped corners.
    int res = int(voxelGridResolution);
    vec3 boxSize = voxelGridBoundsMax - voxelGridBoundsMin;
    vec3 cellSize = boxSize / voxelGridResolution;

    // A zero component would make 1/dir infinite; nudge it instead.
    vec3 dir = rayDir + vec3(equal(rayDir, vec3(0.0))) * 1e-6;
    vec3 invDir = 1.0 / dir;

    float tEntry = max(t.x, 0.0);
    // Nudged a hair inside so the starting cell is not ambiguous when the
    // ray enters exactly on the grid boundary.
    vec3 startPos = viewPos + rayDir * (tEntry + 1e-4 * cellSize.x);
    ivec3 cell = clamp(ivec3((startPos - voxelGridBoundsMin) / cellSize),
                       ivec3(0), ivec3(res - 1));

    ivec3 stepDir = ivec3(sign(dir));
    // Ray t at which the next x/y/z cell boundary is crossed, and how much t
    // one whole cell spans along each axis.
    vec3 nextBoundary = voxelGridBoundsMin
                      + (vec3(cell) + max(vec3(stepDir), 0.0)) * cellSize;
    vec3 tMax = (nextBoundary - viewPos) * invDir;
    vec3 tDelta = abs(cellSize * invDir);

    for (int i = 0; i < 3 * res; i++) {
        // texelFetch at mip 0: this is a "what did voxelization actually
        // write" view, so each voxel is read on its own — no filtering.
        // Mip 0 alpha is binary (1 for occupied), so the first hit is opaque.
        vec4 voxel = texelFetch(voxelGrid, cell, 0);
        if (voxel.a > 0.0) {
            FragColor = vec4(voxel.rgb / voxel.a, 1.0);
            return;
        }

        // Step into whichever neighbor's boundary the ray reaches first.
        if (tMax.x < tMax.y && tMax.x < tMax.z) {
            cell.x += stepDir.x; tMax.x += tDelta.x;
        } else if (tMax.y < tMax.z) {
            cell.y += stepDir.y; tMax.y += tDelta.y;
        } else {
            cell.z += stepDir.z; tMax.z += tDelta.z;
        }

        if (any(lessThan(cell, ivec3(0))) || any(greaterThanEqual(cell, ivec3(res))))
            break;
    }

    FragColor = vec4(background, 1.0);
}
