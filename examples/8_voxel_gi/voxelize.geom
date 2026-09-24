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
// Integer varyings must be flat, and voxelize.frag declares it `flat in` —
// a mismatched interpolation qualifier is a link error.
flat out int mainAxis;

uniform vec3 voxelBoundsMin;
uniform vec3 voxelBoundsMax;

// Single-pass dominant-axis voxelization (Crassin & Green, "Octree-Based
// Sparse Voxelization of Alpha Tested Foliage"). Each triangle is projected
// down whichever world axis its face normal is most aligned with, so it
// rasterizes into the most voxels instead of degenerating to a sliver when
// viewed edge-on down a fixed axis. The chosen axis travels to the fragment
// shader as a flat varying, and the position along it rides in gl_Position.z
// so the rasterizer interpolates a per-fragment slice (gl_FragCoord.z) — a
// triangle sloped across several slices stays sloped instead of collapsing
// into one. Nothing is attached to the framebuffer, so the fragment shader
// reassembles the ivec3 itself and writes through imageAtomicAdd.
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

    for (int i = 0; i < 3; i++) {
        //normalize to min and max, then change to -1, 1
        vec3 gridPos = (gs_in[i].WorldPos - voxelBoundsMin)
                           / (voxelBoundsMax - voxelBoundsMin) * 2.0 - 1.0;
        vec2 flatPos;

        if (axis == 0)
            flatPos = gridPos.yz;
        else if (axis == 1)
            flatPos = gridPos.xz;
        else
            flatPos = gridPos.xy;

        // Depth along the dominant axis. Already in [-1, 1], so the default
        // glDepthRange maps it to gl_FragCoord.z in [0, 1] = grid fraction.
        gl_Position = vec4(flatPos, gridPos[axis], 1.0);

        // Every output is undefined after EmitVertex(), flat ones included,
        // so mainAxis is rewritten per vertex rather than once up front.
        mainAxis = axis;
        WorldPos = gs_in[i].WorldPos;
        Normal = gs_in[i].Normal;
        TexCoords = gs_in[i].TexCoords;
        EmitVertex();
    }
    EndPrimitive();
}
