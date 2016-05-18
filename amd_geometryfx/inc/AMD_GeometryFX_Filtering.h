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
//

#ifndef AMD_GEOMETRYFX_FILTERING_H
#define AMD_GEOMETRYFX_FILTERING_H

// VS2010 has a bug in the headers, where they collide on #defines
// include <intsafe.h> here to get rid of them
#include <intsafe.h>
#include <DirectXMath.h>
#include <d3d11.h>
#include <vector>

#include "AMD_GeometryFX.h"

namespace AMD
{

enum GEOMETRYFX_FILTER
{
    GeometryFX_FilterDuplicateIndices = 0x1,
    GeometryFX_FilterBackface = 0x2,
    GeometryFX_FilterFrustum = 0x8,
    GeometryFX_FilterSmallPrimitives = 0x20,
    GeometryFX_ClusterFilterBackface = 0x1 << 10
};

struct GeometryFX_FilterStatistics
{
    inline GeometryFX_FilterStatistics()
        : trianglesProcessed(0)
        , trianglesRendered(0)
        , trianglesCulled(0)
        , clustersProcessed (0)
        , clustersRendered (0)
        , clustersCulled (0)
    {
    }

    int64 trianglesProcessed;
    int64 trianglesRendered;
    int64 trianglesCulled;
    int64 clustersProcessed;
    int64 clustersRendered;
    int64 clustersCulled;
};

struct GeometryFX_FilterRenderOptions
{
    inline GeometryFX_FilterRenderOptions()
        : enableFiltering(true)
        , enabledFilters(0xFF)
        , statistics(nullptr)
    {
    }

    /**
    If filtering is disabled, the mesh will be rendered directly.
    */
    bool enableFiltering;

    /**
    Specify which filters should be enabled.
    */
    uint32 enabledFilters;

    /**
    If set, statistics counters will be enabled.

    If enabled, queries will be issued along with each draw call significantly
    reducing performance.
    */
    GeometryFX_FilterStatistics *statistics;
};

struct GeometryFX_FilterDesc
{
    inline GeometryFX_FilterDesc()
        : pDevice(nullptr)
        , maximumDrawCallCount(-1)
        , emulateMultiIndirectDraw(false)
    {
    }

    ID3D11Device *pDevice;

    // This is only used if filtering is disabled. If set to -1, it assumes
    // every mesh is drawn exactly once. If instancing is used, each instance
    // counts as a separate draw call.
    int maximumDrawCallCount;

    // Emulate indirect draw. If the extension is present, it will be not used.
    bool emulateMultiIndirectDraw;
};

/**
All resources created here will have names set using DXUT_SetDebugName with a
[AMD GeometryFX Filtering] prefix.
*/
class AMD_GEOMETRYFX_DLL_API GeometryFX_Filter
{
  public:
    struct Handle;
    typedef Handle *MeshHandle;

    GeometryFX_Filter(const GeometryFX_FilterDesc *pFilterDesc);
    ~GeometryFX_Filter();

    /**
    Register meshes for the static mesh renderer.

    This function must be called exactly once.

    @note This function may call functions on the ID3D11Device.
    */
    std::vector<MeshHandle> RegisterMeshes(
        const int meshCount, const int *pVerticesInMesh, const int *pIndicesInMesh);

    /**
    Set the data for a mesh.

    RegisterMeshes() must have been called previously.

    @note This function may call functions on the ID3D11Device and the
        immediate context.
    */
    void SetMeshData(const MeshHandle &handle, const void *pVertexData, const void *pIndexData);

    /**
    Start a render pass.

    From here on, the context should no longer be used by the application
    until EndRender() has been called.

    @note If the multi-indirect-draw extension is present, the context must be
    equal to the immediate context.

    @note A render pass will change the D3D device state. In particular, the
        following states will be changed:

        - vertex shader, pixel shader and compute shader (the library assumes no
            hull or domain shader is bound)
        - resources bound to the vertex shader, pixel shader and compute shader
        - the topology
    */
    void BeginRender(ID3D11DeviceContext *pContext, const GeometryFX_FilterRenderOptions &options,
        const DirectX::XMMATRIX &view, const DirectX::XMMATRIX &projection,
        const int renderTargetWidth, const int renderTargetHeight);

    /**
    Render a mesh.

    Only valid within a BeginRender/EndRender pair. This function will render
    the mesh with the specified world matrix.
    */
    void RenderMesh(const MeshHandle &handle, const DirectX::XMMATRIX &world);

    /**
    Render a mesh with instancing.

    Only valid within a BeginRender/EndRender pair. This function will render
    a number of instances, each with its own world matrix.
    */
    void RenderMeshInstanced(
        const MeshHandle &handle, const int instanceCount, const DirectX::XMMATRIX *pWorldMatrices);

    /**
    End a render pass.

    This function will call functions on the context passed to BeginRender().
    */
    void EndRender();

    /**
    Get the buffers for a mesh.

    If a parameter is set to null, it won't be written.
    */
    void GetBuffersForMesh(const MeshHandle &handle,
        ID3D11Buffer **ppVertexBuffer,
        int32 *pVertexOffset,
        ID3D11Buffer **ppIndexBuffer,
        int32 *pIndexOffset) const;

    /**
    Get info about a mesh.
    */
    void GetMeshInfo(const MeshHandle &handle, int32 *pIndexCount) const;

  private:
    // Disable the copy constructor
    GeometryFX_Filter(const GeometryFX_Filter &);
    GeometryFX_Filter &operator=(const GeometryFX_Filter &);

    struct GeometryFX_OpaqueFilterDesc;
    GeometryFX_OpaqueFilterDesc *impl_;
};

} // namespace AMD

#endif // AMD_GEOMETRYFX_FILTERING_H
