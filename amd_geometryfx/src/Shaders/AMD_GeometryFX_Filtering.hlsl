//
// Copyright (c) 2016 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

cbuffer DrawCallConstantBuffer  : register(b0)
{
    matrix  world               : packoffset(c0);
    matrix  worldView           : packoffset(c4);
    uint    meshIndex           : packoffset(c8.x);
}

cbuffer FrameConstantBuffer     : register(b1)
{
    matrix  view                : packoffset(c0);
    matrix  projection          : packoffset(c4);
    uint    cullFlags           : packoffset(c8.x);
    uint    windowWidth         : packoffset(c8.y);
    uint    windowHeight        : packoffset(c8.z);
};

struct MeshConstants
{
    uint    vertexCount;
    uint    faceCount;
    uint    indexOffset;
    uint    vertexOffset;
};

struct Vertex
{
    float3 p;
};

struct IndirectArgs
{
    uint IndexCountPerInstance;
    uint InstanceCount;
    uint StartIndexLocation;
    int BaseVertexLocation;
    uint StartInstanceLocation;
};

struct SmallBatchDrawConstants
{
    matrix  world;
    matrix  worldView;
    uint    meshIndex;
    uint    padding [3];
};

struct SmallBatchData
{
    uint    meshIndex;
    uint    indexOffset;
    uint    faceCount;
    uint    outputIndexOffset;
    uint    drawIndex;
    uint    drawBatchStart;
};

#define CULL_INDEX_FILTER     0x1
#define CULL_BACKFACE         0x2
#define CULL_FRUSTUM          0x8
#define CULL_SMALL_PRIMITIVES  0x20

#define ENABLE_CULL_INDEX           1
#define ENABLE_CULL_BACKFACE        1
#define ENABLE_CULL_FRUSTUM         1
#define ENABLE_CULL_SMALL_PRIMITIVES 1

RWBuffer<uint>                  filteredIndices             : register(u0);
RWBuffer<uint>                  indirectArgs                : register(u1);

ByteAddressBuffer                           vertexData      : register(t0);
Buffer<uint>                                indexData       : register(t1);
StructuredBuffer<MeshConstants>             meshConstants   : register(t2);
StructuredBuffer<SmallBatchDrawConstants>   drawConstants   : register(t3);
StructuredBuffer<SmallBatchData>            smallBatchData  : register(t4);

float4 DepthOnlyVS(float4 pos : POSITION) : SV_POSITION
{
    return mul (projection, mul (worldView, pos));
}

float4 DepthOnlyMultiIndirectVS (float4 pos : POSITION, uint drawId : DRAWID) : SV_POSITION
{
    return mul (projection, mul (drawConstants [drawId].worldView, pos));
}

float3 LoadVertex (uint index, uint vertexOffset)
{
    return asfloat(vertexData.Load3(vertexOffset + index * 12));
}

bool CullTriangle (uint indices [3], float4 vertices [3])
{
    bool cull = false;

#ifdef ENABLE_CULL_INDEX
    if (cullFlags & CULL_INDEX_FILTER)
    {
        if (   indices[0] == indices[1]
            || indices[1] == indices[2]
            || indices[0] == indices[2])
        {
            cull = true;
        }
    }
#endif

    // Culling in homogenous coordinates
    // Read: "Triangle Scan Conversion using 2D Homogeneous Coordinates"
    //       by Marc Olano, Trey Greer
    //       http://www.cs.unc.edu/~olano/papers/2dh-tri/2dh-tri.pdf
    float3x3 m =
    {
        vertices[0].xyw, vertices[1].xyw, vertices[2].xyw
    };

#if ENABLE_CULL_BACKFACE
    if (cullFlags & CULL_BACKFACE)
    {
        cull = cull || (determinant (m) > 0);
    }
#endif

#if ENABLE_CULL_FRUSTUM || ENABLE_CULL_SMALL_PRIMITIVES
    // Transform vertices[i].xy into normalized 0..1 screen space
    uint verticesInFrontOfNearPlane = 0;
    for (uint i = 0; i < 3; ++i)
    {
        vertices[i].xy /= vertices[i].w;
        vertices[i].xy /= 2;
        vertices[i].xy += float2(0.5, 0.5);
        if (vertices[i].w < 0)
        {
            ++verticesInFrontOfNearPlane;
        }
    }
    bool intersectNearPlane = verticesInFrontOfNearPlane > 0;
#endif

#if ENABLE_CULL_SMALL_PRIMITIVES
    if (cullFlags & CULL_SMALL_PRIMITIVES)
    {
        static const uint SUBPIXEL_BITS = 8;
        static const uint SUBPIXEL_MASK = 0xFF;
        static const uint SUBPIXEL_SAMPLES = 1 << SUBPIXEL_BITS;
        /**
        Computing this in float-point is not precise enough
        We switch to a 23.8 representation here which should match the
        HW subpixel resolution.
        We use a 8-bit wide guard-band to avoid clipping. If
        a triangle is outside the guard-band, it will be ignored.

        That is, the actual viewport supported here is 31 bit, one bit is
        unused, and the guard band is 1 << 23 bit large (8388608 pixels)
        */

        int2 minBB = int2(1 << 30, 1 << 30);
        int2 maxBB = int2(-(1 << 30), -(1 << 30));

        bool insideGuardBand = true;
        for (uint i = 0; i < 3; ++i)
        {
            float2 screenSpacePositionFP = vertices[i].xy * float2 (windowWidth, windowHeight);
            // Check if we would overflow after conversion
            if (   screenSpacePositionFP.x < -(1 << 23)
                || screenSpacePositionFP.x >  (1 << 23)
                || screenSpacePositionFP.y < -(1 << 23)
                || screenSpacePositionFP.y >  (1 << 23))
            {
                insideGuardBand = false;
            }

            int2 screenSpacePosition = int2 (screenSpacePositionFP * SUBPIXEL_SAMPLES);
            minBB = min (screenSpacePosition, minBB);
            maxBB = max (screenSpacePosition, maxBB);
        }

        if (!intersectNearPlane && insideGuardBand)
        {
            /**
            Test is:

            Is the minimum of the bounding box right or above the sample
            point and is the width less than the pixel width in samples in
            one direction.

            This will also cull very long triangles which fall between
            multiple samples.
            */
            cull = cull
            || (
                    ((minBB.x & SUBPIXEL_MASK) > SUBPIXEL_SAMPLES/2)
                &&  ((maxBB.x - ((minBB.x & ~SUBPIXEL_MASK) + SUBPIXEL_SAMPLES/2)) < (SUBPIXEL_SAMPLES - 1)))
            || (
                    ((minBB.y & SUBPIXEL_MASK) > SUBPIXEL_SAMPLES/2)
                &&  ((maxBB.y - ((minBB.y & ~SUBPIXEL_MASK) + SUBPIXEL_SAMPLES/2)) < (SUBPIXEL_SAMPLES - 1)));
        }
    }
#endif

#if ENABLE_CULL_FRUSTUM
    if (cullFlags & CULL_FRUSTUM)
    {
        // Cull vertices that are entirely behind the viewer
        cull = cull || (verticesInFrontOfNearPlane == 3);

        if (!intersectNearPlane)
        {
            float minx = min (min (vertices[0].x, vertices[1].x), vertices[2].x);
            float miny = min (min (vertices[0].y, vertices[1].y), vertices[2].y);
            float maxx = max (max (vertices[0].x, vertices[1].x), vertices[2].x);
            float maxy = max (max (vertices[0].y, vertices[1].y), vertices[2].y);

            cull = cull || (maxx < 0) || (maxy < 0) || (minx > 1) || (miny > 1);
        }

    }
#endif

    return cull;
}

#ifdef AMD_COMPILE_COMPUTE_SHADER
groupshared uint workGroupOutputSlot;
groupshared uint workGroupIndexCount;

#ifndef SMALL_BATCH_COUNT
#define SMALL_BATCH_COUNT 256
#endif

#ifndef SMALL_BATCH_SIZE
#define SMALL_BATCH_SIZE 256
#endif

[numthreads(SMALL_BATCH_SIZE, 1, 1 )]
void FilterCS(
    uint3 inGroupId : SV_GroupThreadID,
    uint3 groupId : SV_GroupID )
{
    if (inGroupId.x == 0)
    {
        workGroupIndexCount = 0;
    }

    GroupMemoryBarrierWithGroupSync ();

    bool cull = true;
    uint threadOutputSlot = 0;

    uint batchMeshIndex = smallBatchData [groupId.x].meshIndex;
    uint batchInputIndexOffset = (meshConstants [batchMeshIndex].indexOffset + smallBatchData [groupId.x].indexOffset) / 4;
    uint batchInputVertexOffset = meshConstants [batchMeshIndex].vertexOffset;
    uint batchDrawIndex = smallBatchData [groupId.x].drawIndex;

    if (inGroupId.x < smallBatchData [groupId.x].faceCount)
    {
        float4x4 worldView = drawConstants [batchDrawIndex].worldView;

        uint indices [3] =
        {
            indexData [inGroupId.x * 3 + 0 + batchInputIndexOffset],
            indexData [inGroupId.x * 3 + 1 + batchInputIndexOffset],
            indexData [inGroupId.x * 3 + 2 + batchInputIndexOffset]
        };

        float4 vertices [3] =
        {
            mul (projection, mul (worldView, float4 (LoadVertex (indices [0], batchInputVertexOffset), 1))),
            mul (projection, mul (worldView, float4 (LoadVertex (indices [1], batchInputVertexOffset), 1))),
            mul (projection, mul (worldView, float4 (LoadVertex (indices [2], batchInputVertexOffset), 1)))
        };

        cull = CullTriangle (indices, vertices);

        if (!cull)
        {
            InterlockedAdd (workGroupIndexCount, 3, threadOutputSlot);
        }
    }

    GroupMemoryBarrierWithGroupSync ();

    if (inGroupId.x == 0)
    {
        InterlockedAdd (indirectArgs [batchDrawIndex * 5], workGroupIndexCount, workGroupOutputSlot);
    }

    AllMemoryBarrierWithGroupSync ();

    uint outputIndexOffset =  workGroupOutputSlot + smallBatchData [groupId.x].outputIndexOffset / 4;

    if (!cull)
    {
        filteredIndices [outputIndexOffset + threadOutputSlot + 0] = indexData [inGroupId.x * 3 + 0 + batchInputIndexOffset];
        filteredIndices [outputIndexOffset + threadOutputSlot + 1] = indexData [inGroupId.x * 3 + 1 + batchInputIndexOffset];
        filteredIndices [outputIndexOffset + threadOutputSlot + 2] = indexData [inGroupId.x * 3 + 2 + batchInputIndexOffset];
    }

    if (inGroupId.x == 0 && groupId.x == smallBatchData [groupId.x].drawBatchStart)
    {
        indirectArgs [batchDrawIndex * 5 + 2] = smallBatchData[groupId.x].outputIndexOffset / 4; // 4 == sizeof (int32)
        indirectArgs [batchDrawIndex * 5 + 3] = batchInputVertexOffset / 12; // 12 == sizeof (float3)
        indirectArgs [batchDrawIndex * 5 + 4] = batchDrawIndex;
    }
}

#define CLEAR_THREAD_COUNT 256

[numthreads (CLEAR_THREAD_COUNT, 1, 1)]
void ClearDrawIndirectArgsCS (uint3 id : SV_DispatchThreadID)
{
    indirectArgs[id.x * 5 + 0] = 0;
}
#endif
