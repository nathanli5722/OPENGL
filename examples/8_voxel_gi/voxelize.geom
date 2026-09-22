#version 420 core

layout (triangles) in;
layout (triangle_strip, max_vertices = 3) out;

in VS_OUT {
    vec3 WorldPos;
    vec3 Normal;
    vec2 TexCoords;
} gs_in[];

out vec3 WorldPos;
out vec3 Normal;
out vec2 TexCoords;
// Integer varyings must be flat, and voxelize.frag declares them `flat in` —
// a mismatched interpolation qualifier is a link error.
flat out int layer;
flat out int mainAxis;

uniform vec3 voxelBoundsMin;
uniform vec3 voxelBoundsMax;
uniform int voxelResolution;

// Single-pass dominant-axis voxelization (Crassin & Green, "Octree-Based
// Sparse Voxelization of Alpha Tested Foliage"). Each triangle is projected
// down whichever world axis its face normal is most aligned with, so it
// rasterizes into the most voxels instead of degenerating to a sliver when
// viewed edge-on down a fixed axis. The chosen axis and slice index travel to
// the fragment shader as flat varyings rather than via gl_Layer: nothing is
// attached to the framebuffer, so there are no layers to route to — the
// fragment shader reassembles the ivec3 itself and writes through
// imageAtomicAdd.
void main()
{
    //find face normal (vertex normals aren't accurate)
    vec3 faceNormal = abs(normalize(cross(gs_in[1].WorldPos - gs_in[0].WorldPos, 
                                        gs_in[2].WorldPos - gs_in[0].WorldPos)));
    int axis = 0; // 0 = x, 1 = y, 2 = z
    if (faceNormal.y > faceNormal.x && faceNormal.y > faceNormal.z)
        axis = 1;
    else if (faceNormal.z > faceNormal.x && faceNormal.z > faceNormal.y)
        axis = 2;

    //normalize to min and max, then change to -1, 1. Done for all three up
    //front because the emit loop needs the flattened xy and the slice index
    //needs the triangle's centroid along the dominant axis.
    vec3 gridPos[3];
    for (int i = 0; i < 3; i++)
        gridPos[i] = (gs_in[i].WorldPos - voxelBoundsMin)
                         / (voxelBoundsMax - voxelBoundsMin) * 2.0 - 1.0;

    // One slice for the whole triangle, not one per vertex: `layer` is flat,
    // so a per-vertex value would collapse to the provoking vertex's anyway.
    float centroidAxis =
        (gridPos[0][axis] + gridPos[1][axis] + gridPos[2][axis]) / 3.0;

    mainAxis = axis;
    layer = clamp(int((centroidAxis * 0.5 + 0.5) * voxelResolution),
                  0, voxelResolution - 1);

    for (int i = 0; i < 3; i++) {
        vec2 flatPos;

        if (axis == 0)
            flatPos = gridPos[i].yz;
        else if (axis == 1)
            flatPos = gridPos[i].xz;
        else
            flatPos = gridPos[i].xy;

        gl_Position = vec4(flatPos, 0.0, 1.0);

        WorldPos = gs_in[i].WorldPos;
        Normal = gs_in[i].Normal;
        TexCoords = gs_in[i].TexCoords;
        EmitVertex();
    }
    EndPrimitive();
}
