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

#ifndef __AMD_GEOMETRYFX_FILTERING_H__
#define __AMD_GEOMETRYFX_FILTERING_H__

// VS2010 has a bug in the headers, where they collide on #defines
// include <intsafe.h> here to get rid of them
#include <intsafe.h>
#include <DirectXMath.h>
#include <d3d11.h>
#include <vector>

#include "AMD_GeometryFX.h"

namespace AMD
{
namespace GeometryFX
{

enum FILTER
{
    FilterDuplicateIndices = 0x1,
    FilterBackface = 0x2,
    FilterFrustum = 0x8,
    FilterSmallPrimitives = 0x20
};

struct FilterStatistics
{
    inline FilterStatistics()
        : trianglesProcessed(0)
        , trianglesRendered(0)
        , trianglesCulled(0)
    {
    }

    int64 trianglesProcessed;
    int64 trianglesRendered;
    int64 trianglesCulled;
};

struct FILTER_RENDER_OPTIONS
{
    inline FILTER_RENDER_OPTIONS()
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
    FilterStatistics *statistics;
};

struct FILTER_DESC
{
    inline FILTER_DESC()
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
class AMD_GEOMETRYFX_DLL_API Filter
{
  public:
    struct Handle;
    typedef Handle *MeshHandle;

    Filter(const FILTER_DESC *pFilterDesc);
    ~Filter();

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
    void BeginRender(ID3D11DeviceContext *pContext, const FILTER_RENDER_OPTIONS &options,
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
    Filter(const Filter &);
    Filter &operator=(const Filter &);

    struct FILTER_OPAQUE;
    FILTER_OPAQUE *impl_;
};
} // namespace GeometryFX
} // namespace AMD
#endif // __AMD_GEOMETRYFX_FILTERING_H__