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

uniform vec3 voxelBoundsMin;
uniform vec3 voxelBoundsMax;
uniform int voxelResolution;

// TODO(you): single-pass dominant-axis voxelization (Crassin & Green,
// "Octree-Based Sparse Voxelization of Alpha Tested Foliage"). For this
// triangle:
//   1. Pick whichever world-space axis (X/Y/Z) the triangle's face normal
//      is most aligned with — e.g. from the cross product of its two edges
//      (gs_in[1].WorldPos - gs_in[0].WorldPos) x (gs_in[2].WorldPos -
//      gs_in[0].WorldPos) — so it rasterizes into the most voxels instead
//      of degenerating to a sliver when viewed edge-on down a fixed axis.
//   2. For each of the 3 vertices, map WorldPos from [voxelBoundsMin,
//      voxelBoundsMax] into [-1, 1] grid space, then drop the chosen axis
//      to get gl_Position.xy for that vertex (gl_Position.z/.w can stay
//      fixed — there's no depth test during voxelization).
//   3. Compute gl_Layer once per triangle (not per vertex) from the
//      triangle's position along the chosen axis, mapped to
//      [0, voxelResolution - 1].
//   4. Pass WorldPos/Normal/TexCoords through unchanged to voxelize.frag,
//      EmitVertex() per vertex, then EndPrimitive().
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
        vec3 transformedPos = (gs_in[i].WorldPos - voxelBoundsMin) 
                                / (voxelBoundsMax - voxelBoundsMin) * 2.0 - 1.0;

        vec2 flatPos;
        float otherAxis;
        
        if (axis == 0) {
            otherAxis = transformedPos.x;
            flatPos = transformedPos.yz;
        }
        else if (axis == 1) {
            otherAxis = transformedPos.y;
            flatPos = transformedPos.xz;
        }
        else {
            otherAxis = transformedPos.z;
            flatPos = transformedPos.xy;
        }

        gl_Layer = int((otherAxis - 1.0) / 2.0 * voxelResolution);
        gl_Position = vec4(flatPos, 0.0, 1.0);

        WorldPos = gs_in[0].WorldPos;
        Normal = gs_in[0].Normal;
        TexCoords = gs_in[0].TexCoords;
        EmitVertex();
    }
    EndPrimitive();
}
