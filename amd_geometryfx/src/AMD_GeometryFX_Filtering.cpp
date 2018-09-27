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

#include "AMD_GeometryFX_Filtering.h"

#include "Shaders/inc/AMD_GeometryFX_ClearDrawIndirectArgsCS.inc"
#include "Shaders/inc/AMD_GeometryFX_DepthOnlyVS.inc"
#include "Shaders/inc/AMD_GeometryFX_DepthOnlyMultiIndirectVS.inc"
#include "Shaders/inc/AMD_GeometryFX_FilterCS.inc"

// ComPtr
#include <wrl.h>

#include <string>
#include <unordered_map>
#include <vector>
#include <numeric>
#include <algorithm>

#include "GeometryFXMesh.h"
#include "GeometryFXMeshManager.h"

#include "amd_ags.h"

#include "GeometryFXUtility_Internal.h"
#include "AMD_GeometryFX_Internal.h"

#include <d3d11_1.h>

#undef min
#undef max

using namespace DirectX;
using namespace Microsoft::WRL;

namespace AMD
{
using namespace GeometryFX_Internal;

namespace
{

struct FilterContext
{
    const GeometryFX_FilterRenderOptions *options;
    XMMATRIX view;
    XMMATRIX projection;
    XMVECTOR eye;
    int windowWidth;
    int windowHeight;
};

#pragma pack(push, 1)
struct FrameConstantBuffer
{
    XMMATRIX view;
    XMMATRIX projection;
    uint32 cullFlags;
    uint32 width, height;
};

struct DrawCallArguments
{
    XMMATRIX world;
    XMMATRIX worldView;
    uint32 meshIndex;
    uint32 pad[3];
};

struct IndirectArguments
{
    /**
    Static function to ensure IndirectArguments remains a POD
    */
    static void Init (IndirectArguments &ia)
    {
        ia.IndexCountPerInstance = 0;
        ia.InstanceCount = 1;
        ia.StartIndexLocation = 0;
        ia.BaseVertexLocation = 0;
        ia.StartInstanceLocation = 0;
    }

    uint32 IndexCountPerInstance;
    uint32 InstanceCount;
    uint32 StartIndexLocation;
    int32 BaseVertexLocation;
    uint32 StartInstanceLocation;
};

struct SmallBatchData
{
    uint32 meshIndex;         // Index into meshConstants
    uint32 indexOffset;       // Index relative to the meshConstants[meshIndex].indexOffset
    uint32 faceCount;         // Number of faces in this small batch
    uint32 outputIndexOffset; // Offset into the output index buffer
    uint32 drawIndex;         // Index into the SmallBatchDrawCallTable
    uint32 drawBatchStart;    // First slot for the current draw call
};
#pragma pack(pop)

struct DrawCommand
{
    inline DrawCommand ()
        : mesh (nullptr)
        , drawCallId (-1)
        , firstTriangle (0)
    {
    }

    DrawCallArguments dcb;
    GeometryFX_Internal::StaticMesh *mesh;
    int drawCallId;
    int firstTriangle;
};

/**
One small batch chunk can accept multiple draw requests. Draw requests are
split into small batches of TRIANGLES_PER_SMALL_BATCH each. A draw request always
occupies consecutive slots. A draw request may be split if it does not fit
entirely into this small batch.

The filter then processes all small batches in this chunk in one go, and renders
them by using one indirect draw call per original draw request.
*/
class SmallBatchChunk
{
public:
    SmallBatchChunk (ID3D11Device *device, bool emulateMultiDraw, AGSContext* agsContext)
        : smallBatchDataBackingStore_ (SmallBatchMergeConstants::BATCH_COUNT)
        , drawCallBackingStore_ (SmallBatchMergeConstants::BATCH_COUNT)
        , agsContext_ (agsContext)
        , currentBatchCount_ (0)
        , currentDrawCallCount_ (0)
        , faceCount_ (0)
        , useMultiIndirectDraw_ (!emulateMultiDraw)
    {
        CreateFilteredIndexBuffer (device);
        CreateSmallBatchDataBuffer (device);
        CreateIndirectDrawArgumentsBuffer (device);
        CreateDrawCallArgumentsBuffer (device);

        CreateInstanceIdBuffer (device);
    }

    /**
    If true is returned, then remainder has been filled and must be
    re-submitted. Otherwise, the whole request has been handled by this small
    batch.
    */
    bool AddRequest (const DrawCommand &request, DrawCommand &remainder,
        FilterContext &filterContext)
    {
        if (currentDrawCallCount_ == SmallBatchMergeConstants::BATCH_COUNT)
        {
            remainder = request;
            return true;
        }
        
        assert (request.firstTriangle >= 0);

        int firstTriangle = request.firstTriangle;
        const int firstCluster = firstTriangle / SmallBatchMergeConstants::BATCH_SIZE;
        int currentCluster = firstCluster;
        int lastTriangle = firstTriangle;

        const int filteredIndexBufferStartOffset =
            currentBatchCount_ * SmallBatchMergeConstants::BATCH_SIZE * 3 * sizeof (int);

        const int firstBatch = currentBatchCount_;

        // We move the eye position into object space, so we don't have to
        // transform the cone into world space all the time
        // This matrix inversion will happen once every 2^16 triangles on
        // average; and saves us transforming the cone every 256 triangles
        const auto eye = DirectX::XMVector4Transform (filterContext.eye, XMMatrixInverse (nullptr, request.dcb.world));
        
        // Try to assign batches until we run out of batches or geometry
        for (int i = currentBatchCount_; i < SmallBatchMergeConstants::BATCH_COUNT; ++i)
        {
            lastTriangle = std::min (
                firstTriangle + SmallBatchMergeConstants::BATCH_SIZE, request.mesh->faceCount);

            assert (currentCluster < static_cast<int>(request.mesh->clusters.size ()));
            const auto& clusterInfo = request.mesh->clusters[currentCluster];
            ++currentCluster;

            bool cullCluster = false;

            if (((filterContext.options->enabledFilters & GeometryFX_ClusterFilterBackface) != 0) && clusterInfo.valid)
            {
                const auto testVec = DirectX::XMVector3Normalize (DirectX::XMVectorSubtract (eye, clusterInfo.coneCenter));
                // Check if we're inside the cone
                if (DirectX::XMVectorGetX (DirectX::XMVector3Dot (testVec, clusterInfo.coneAxis)) > clusterInfo.coneAngleCosine)
                {
                    cullCluster = true;
                }
            }

            if (!cullCluster)
            {
                auto &smallBatchData = smallBatchDataBackingStore_[currentBatchCount_];

                smallBatchData.drawIndex = currentDrawCallCount_;
                smallBatchData.faceCount = lastTriangle - firstTriangle;

                // Offset relative to the start of the mesh
                smallBatchData.indexOffset = firstTriangle * 3 * sizeof (int);
                smallBatchData.outputIndexOffset = filteredIndexBufferStartOffset;
                smallBatchData.meshIndex = request.dcb.meshIndex;
                smallBatchData.drawBatchStart = firstBatch;

                faceCount_ += smallBatchData.faceCount;

                ++currentBatchCount_;
            }

            firstTriangle += SmallBatchMergeConstants::BATCH_SIZE;

            if (lastTriangle == request.mesh->faceCount)
            {
                break;
            }
        }

        if (filterContext.options->statistics)
        {
            filterContext.options->statistics->clustersProcessed +=
                currentCluster - firstCluster;

            filterContext.options->statistics->clustersRendered +=
                currentBatchCount_ - firstBatch;

            filterContext.options->statistics->clustersCulled +=
                filterContext.options->statistics->clustersProcessed - filterContext.options->statistics->clustersRendered;
        }

        if (currentBatchCount_ > firstBatch)
        {
            drawCallBackingStore_[currentDrawCallCount_] = request.dcb;
            ++currentDrawCallCount_;
        }

        // Check if the draw command fit into this call, if not, create a remainder
        if (lastTriangle < request.mesh->faceCount)
        {
            remainder = request;
            assert (lastTriangle >= 0);
            remainder.firstTriangle = lastTriangle;

            return true;
        }
        else
        {
            return false;
        }
    }

    void Render (ID3D11DeviceContext *context, ID3D11ComputeShader *computeClearShader,
        ID3D11ComputeShader *filterShader, ID3D11VertexShader *vertexShader,
        ID3D11ShaderResourceView *vertexData, ID3D11ShaderResourceView *indexData,
        ID3D11ShaderResourceView *meshConstantData, ID3D11Buffer *globalVertexBuffer,
        ID3D11Buffer *perFrameConstantBuffer)
    {
        ClearIndirectArgsBuffer (context, computeClearShader);
        UpdateDrawCallAndSmallBatchBuffers (context);
        Filter (context, filterShader, vertexData, indexData, meshConstantData,
            perFrameConstantBuffer);

        context->VSSetShader (vertexShader, nullptr, 0);

        context->IASetIndexBuffer (filteredIndexBuffer_.Get (), DXGI_FORMAT_R32_UINT, 0);
        context->IASetPrimitiveTopology (D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        ID3D11Buffer *iaVBs[] = { globalVertexBuffer, instanceIdBuffer_.Get () };
        UINT vbOffsets[] = { 0, 0 };
        UINT vbStrides[] = { sizeof (float) * 3, sizeof (int) };
        context->IASetVertexBuffers (0, 2, iaVBs, vbStrides, vbOffsets);

        ID3D11ShaderResourceView *srvs[] = { drawCallSRV_.Get () };

        context->VSSetShaderResources (3, 1, srvs);

        if (agsContext_ && useMultiIndirectDraw_)
        {
            agsDriverExtensions_MultiDrawIndexedInstancedIndirect (agsContext_,
                currentDrawCallCount_,
                indirectArgumentsBuffer_.Get (), 0, sizeof (IndirectArguments));
        }
        else
        {
            for (int i = 0; i < currentDrawCallCount_; ++i)
            {
                context->DrawIndexedInstancedIndirect (
                    indirectArgumentsBuffer_.Get (), sizeof (IndirectArguments) * i);
            }
        }

        context->IASetIndexBuffer (nullptr, DXGI_FORMAT_R32_UINT, 0);

        Reset ();
    }

    int GetFaceCount () const
    {
        return faceCount_;
    }

private:
    void Filter (ID3D11DeviceContext *context, ID3D11ComputeShader *filterShader,
        ID3D11ShaderResourceView *vertexData, ID3D11ShaderResourceView *indexData,
        ID3D11ShaderResourceView *meshConstantData, ID3D11Buffer *perFrameConstantBuffer) const
    {
        ID3D11ShaderResourceView *csSRVs[] = { vertexData, indexData, meshConstantData, drawCallSRV_.Get (), smallBatchDataSRV_.Get () };
        context->CSSetShaderResources (0, 5, csSRVs);

        UINT initialCounts[] = { 0, 0 };
        ID3D11UnorderedAccessView *csUAVs[] = { filteredIndexUAV_.Get (), indirectArgumentsUAV_.Get () };
        context->CSSetUnorderedAccessViews (0, 2, csUAVs, initialCounts);

        ID3D11Buffer *csCBs[] = { perFrameConstantBuffer };
        context->CSSetConstantBuffers (1, 1, csCBs);

        context->CSSetShader (filterShader, nullptr, 0);
        
        context->Dispatch (currentBatchCount_, 1, 1);

        csUAVs[0] = nullptr;
        csUAVs[1] = nullptr;
        context->CSSetUnorderedAccessViews (0, 2, csUAVs, initialCounts);
    }

    void UpdateDrawCallAndSmallBatchBuffers (ID3D11DeviceContext *context) const
    {
        D3D11_MAPPED_SUBRESOURCE mapping;
        context->Map (smallBatchDataBuffer_.Get (), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapping);

        ::memcpy (mapping.pData, smallBatchDataBackingStore_.data (),
            sizeof (SmallBatchData) * smallBatchDataBackingStore_.size ());

        context->Unmap (smallBatchDataBuffer_.Get (), 0);

        context->Map (drawCallBuffer_.Get (), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapping);

        ::memcpy (mapping.pData, drawCallBackingStore_.data (),
            sizeof (DrawCallArguments) * drawCallBackingStore_.size ());

        context->Unmap (drawCallBuffer_.Get (), 0);
    }

    void CreateFilteredIndexBuffer (ID3D11Device *device)
    {
        D3D11_BUFFER_DESC filteredIndexBufferDesc = {};
        filteredIndexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER | D3D11_BIND_UNORDERED_ACCESS;
        filteredIndexBufferDesc.ByteWidth = SmallBatchMergeConstants::BATCH_COUNT *
            SmallBatchMergeConstants::BATCH_SIZE *
            (sizeof (int) * 3);
        filteredIndexBufferDesc.CPUAccessFlags = 0;
        filteredIndexBufferDesc.MiscFlags = 0;
        filteredIndexBufferDesc.Usage = D3D11_USAGE_DEFAULT;

        device->CreateBuffer (&filteredIndexBufferDesc, nullptr, &filteredIndexBuffer_);
        SetDebugName (filteredIndexBuffer_.Get (), "[AMD GeometryFX Filtering] Filtered index buffer [%p]", this);

        D3D11_UNORDERED_ACCESS_VIEW_DESC fibUav = {};
        fibUav.Buffer.FirstElement = 0;
        fibUav.Buffer.Flags = 0;
        fibUav.Buffer.NumElements =
            SmallBatchMergeConstants::BATCH_COUNT * SmallBatchMergeConstants::BATCH_SIZE * 3;
        fibUav.Format = DXGI_FORMAT_R32_UINT;
        fibUav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;

        device->CreateUnorderedAccessView (filteredIndexBuffer_.Get (), &fibUav, &filteredIndexUAV_);
        SetDebugName (filteredIndexUAV_.Get (), "[AMD GeometryFX Filtering] Filtered index buffer UAV [%p]", this);
    }

    void CreateSmallBatchDataBuffer (ID3D11Device *device)
    {
        D3D11_BUFFER_DESC smallBatchDataBufferDesc;
        smallBatchDataBufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        smallBatchDataBufferDesc.ByteWidth =
            SmallBatchMergeConstants::BATCH_COUNT * sizeof (SmallBatchData);
        smallBatchDataBufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        smallBatchDataBufferDesc.StructureByteStride = sizeof (SmallBatchData);
        smallBatchDataBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        smallBatchDataBufferDesc.Usage = D3D11_USAGE_DYNAMIC;

        device->CreateBuffer (&smallBatchDataBufferDesc, nullptr, &smallBatchDataBuffer_);
        SetDebugName (smallBatchDataBuffer_.Get (), "[AMD GeometryFX Filtering] Batch data buffer [%p]", this);

        D3D11_SHADER_RESOURCE_VIEW_DESC smallBatchDataSRVDesc;
        smallBatchDataSRVDesc.Buffer.FirstElement = 0;
        smallBatchDataSRVDesc.Buffer.NumElements = SmallBatchMergeConstants::BATCH_COUNT;
        smallBatchDataSRVDesc.Format = DXGI_FORMAT_UNKNOWN;
        smallBatchDataSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;

        device->CreateShaderResourceView (
            smallBatchDataBuffer_.Get (), &smallBatchDataSRVDesc, &smallBatchDataSRV_);
        SetDebugName (smallBatchDataSRV_.Get (), "[AMD GeometryFX Filtering] Batch data buffer SRV [%p]", this);
    }

    void CreateIndirectDrawArgumentsBuffer (ID3D11Device *device)
    {
        D3D11_BUFFER_DESC indirectArgumentsBufferDesc;
        indirectArgumentsBufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        indirectArgumentsBufferDesc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
        indirectArgumentsBufferDesc.ByteWidth =
            sizeof (IndirectArguments) * SmallBatchMergeConstants::BATCH_COUNT;
        indirectArgumentsBufferDesc.StructureByteStride = sizeof (IndirectArguments);
        indirectArgumentsBufferDesc.CPUAccessFlags = 0;
        indirectArgumentsBufferDesc.Usage = D3D11_USAGE_DEFAULT;

        std::vector<IndirectArguments> indirectArgs (SmallBatchMergeConstants::BATCH_COUNT);

        for (int i = 0; i < SmallBatchMergeConstants::BATCH_COUNT; ++i)
        {
            IndirectArguments::Init (indirectArgs[i]);
        }

        D3D11_SUBRESOURCE_DATA indirectArgumentsBufferData;
        indirectArgumentsBufferData.pSysMem = indirectArgs.data ();
        indirectArgumentsBufferData.SysMemPitch =
            static_cast<UINT>(sizeof (IndirectArguments) * indirectArgs.size ());
        indirectArgumentsBufferData.SysMemSlicePitch = indirectArgumentsBufferData.SysMemPitch;

        device->CreateBuffer (
            &indirectArgumentsBufferDesc, &indirectArgumentsBufferData, &indirectArgumentsBuffer_);

        SetDebugName (indirectArgumentsBuffer_.Get (), "[AMD GeometryFX Filtering] Indirect arguments buffer [%p]", this);

        D3D11_UNORDERED_ACCESS_VIEW_DESC indirectArgsUAVDesc = {};
        indirectArgsUAVDesc.Buffer.FirstElement = 0;
        indirectArgsUAVDesc.Buffer.NumElements = SmallBatchMergeConstants::BATCH_COUNT * 5;
        indirectArgsUAVDesc.Format = DXGI_FORMAT_R32_UINT;
        indirectArgsUAVDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;

        device->CreateUnorderedAccessView (
            indirectArgumentsBuffer_.Get (), &indirectArgsUAVDesc, &indirectArgumentsUAV_);
        SetDebugName (indirectArgumentsUAV_.Get (), "[AMD GeometryFX Filtering] Indirect arguments buffer UAV [%p]", this);
    }

    void CreateDrawCallArgumentsBuffer (ID3D11Device *device)
    {
        D3D11_BUFFER_DESC drawCallBufferDesc;
        drawCallBufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        drawCallBufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        drawCallBufferDesc.ByteWidth =
            sizeof (DrawCallArguments) * SmallBatchMergeConstants::BATCH_COUNT;
        drawCallBufferDesc.StructureByteStride = sizeof (DrawCallArguments);
        drawCallBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        drawCallBufferDesc.Usage = D3D11_USAGE_DYNAMIC;

        device->CreateBuffer (&drawCallBufferDesc, nullptr, &drawCallBuffer_);
        SetDebugName (drawCallBuffer_.Get (), "[AMD GeometryFX Filtering] Draw arguments buffer [%p]", this);

        D3D11_SHADER_RESOURCE_VIEW_DESC drawCallSRVDesc;
        drawCallSRVDesc.Buffer.FirstElement = 0;
        drawCallSRVDesc.Buffer.NumElements = SmallBatchMergeConstants::BATCH_COUNT;
        drawCallSRVDesc.Format = DXGI_FORMAT_UNKNOWN;
        drawCallSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;

        device->CreateShaderResourceView (drawCallBuffer_.Get (), &drawCallSRVDesc, &drawCallSRV_);
        SetDebugName (drawCallSRV_.Get (), "[AMD GeometryFX Filtering] Draw arguments buffer SRV [%p]", this);
    }

    /**
    The instance ID buffer is our workaround for not having gl_DrawID in D3D.
    The buffer simply contains 0, 1, 2, 3 ..., and is bound with a per-instance
    rate of 1.
    */
    void CreateInstanceIdBuffer (ID3D11Device *device)
    {
        D3D11_BUFFER_DESC instanceIdBufferDesc = {};
        instanceIdBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        instanceIdBufferDesc.ByteWidth = sizeof (int) * SmallBatchMergeConstants::BATCH_COUNT;
        instanceIdBufferDesc.StructureByteStride = sizeof (int);
        instanceIdBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;

        std::vector<int> ids (SmallBatchMergeConstants::BATCH_COUNT);
        std::iota (ids.begin (), ids.end (), 0);

        D3D11_SUBRESOURCE_DATA data;
        data.pSysMem = ids.data ();
        data.SysMemPitch = instanceIdBufferDesc.ByteWidth;
        data.SysMemSlicePitch = data.SysMemPitch;

        device->CreateBuffer (&instanceIdBufferDesc, &data, &instanceIdBuffer_);
        SetDebugName (instanceIdBuffer_.Get (), "[AMD GeometryFX Filtering] Instance ID buffer [%p]", this);
    }

    void Reset ()
    {
        currentBatchCount_ = 0;
        currentDrawCallCount_ = 0;
        faceCount_ = 0;
    }

    void ClearIndirectArgsBuffer (
        ID3D11DeviceContext *context, ID3D11ComputeShader *computeClearShader) const
    {
        ID3D11UnorderedAccessView *uavViews[] = { indirectArgumentsUAV_.Get () };
        UINT initialCounts[] = { 0 };
        context->CSSetUnorderedAccessViews (1, 1, uavViews, initialCounts);
        context->CSSetShader (computeClearShader, nullptr, 0);
        context->Dispatch (currentBatchCount_, 1, 1);

        uavViews[0] = nullptr;

        context->CSSetUnorderedAccessViews (0, 1, uavViews, initialCounts);
    }

    ComPtr<ID3D11Buffer> smallBatchDataBuffer_;
    ComPtr<ID3D11ShaderResourceView> smallBatchDataSRV_;
    ComPtr<ID3D11Buffer> filteredIndexBuffer_;
    ComPtr<ID3D11UnorderedAccessView> filteredIndexUAV_;
    ComPtr<ID3D11Buffer> indirectArgumentsBuffer_;
    ComPtr<ID3D11UnorderedAccessView> indirectArgumentsUAV_;
    ComPtr<ID3D11Buffer> drawCallBuffer_;
    ComPtr<ID3D11ShaderResourceView> drawCallSRV_;

    std::vector<ComPtr<ID3D11Buffer>> drawCallConstantBuffers_;
    ComPtr<ID3D11Buffer> drawCallConstantBufferMerged_;

    ComPtr<ID3D11Buffer> instanceIdBuffer_;

    std::vector<SmallBatchData> smallBatchDataBackingStore_;
    std::vector<DrawCallArguments> drawCallBackingStore_;

    int currentBatchCount_;
    int currentDrawCallCount_;
    int faceCount_;

    bool useMultiIndirectDraw_;
    AGSContext* agsContext_;
};
}

struct GeometryFX_Filter::Handle
{
    Handle()
        : index(-1)
        , mesh(nullptr)
    {
    }

    Handle(int index)
        : index(index)
        , mesh(nullptr)
    {
    }

    int index;
    GeometryFX_Internal::StaticMesh *mesh;
};

struct GeometryFX_Filter::GeometryFX_OpaqueFilterDesc
{
    enum SMALL_BATCH_CHUNK
    {
        SMALL_BATCH_CHUNK_COUNT = 16
    };

public:
    void *operator new (size_t sz) throw()
    {
        return _aligned_malloc(sz, 16);
    }

        void operator delete (void *p) throw()
    {
        _aligned_free(p);
    }

    GeometryFX_OpaqueFilterDesc(const GeometryFX_FilterDesc &createInfo)
        : device_(createInfo.pDevice)
        , maxDrawCallCount_(createInfo.maximumDrawCallCount)
        , emulateMultiDrawIndirect_(false)
        , currentDrawCall_(0)
        , deviceContext_(nullptr)
    {
        CreateQueries();
        CreateConstantBuffers();
        CreateShaders();

        meshManager_ = GeometryFX_Internal::CreateGlobalMeshManager();
        agsContext_ = nullptr;

        if (agsInit(&agsContext_, nullptr, nullptr) == AGS_SUCCESS)
        {
            unsigned int supportedExtensions = 0;
            agsDriverExtensions_Init(agsContext_, device_, &supportedExtensions);

            if ((supportedExtensions & AGS_EXTENSION_MULTIDRAWINDIRECT) == 0)
            {
                OutputDebugString(TEXT("AGS initialized but multi draw extension not supported"));
                agsDriverExtensions_DeInit(agsContext_);
                agsDeInit(agsContext_);
                agsContext_ = nullptr;
            }

            if (createInfo.emulateMultiIndirectDraw)
            {
                OutputDebugString(TEXT("Multi draw extension supported but ignored"));
                emulateMultiDrawIndirect_ = true;
            }
        }
    }

    ~GeometryFX_OpaqueFilterDesc()
    {
        if (agsContext_)
        {
            agsDriverExtensions_DeInit(agsContext_);
            agsDeInit(agsContext_);
        }
    }

    std::vector<MeshHandle> RegisterMeshes(
        const int meshCount, const int *verticesInMesh, const int *indicesInMesh)
    {
        handles_.resize(meshCount);
        for (int i = 0; i < meshCount; ++i)
        {
            handles_[i].reset(new Handle(i));
        }

        meshManager_->Allocate(device_, meshCount, verticesInMesh, indicesInMesh);

        for (int i = 0; i < meshCount; ++i)
        {
            handles_[i]->mesh = meshManager_->GetMesh(i);
        }

        CreateIndirectDrawArgumentsBuffer(meshCount, indicesInMesh, verticesInMesh);

        if (maxDrawCallCount_ == -1)
        {
            maxDrawCallCount_ = static_cast<int>(meshManager_->GetMeshCount());
        }

        CreateDrawCallConstantBuffers();

        for (int i = 0; i < SMALL_BATCH_CHUNK_COUNT; ++i)
        {
            smallBatchChunks_.emplace_back(
                new SmallBatchChunk(device_, emulateMultiDrawIndirect_, agsContext_));
        }

        std::vector<MeshHandle> result;
        result.reserve(handles_.size());
        for (std::vector<std::unique_ptr<Handle>>::iterator it = handles_.begin(),
            end = handles_.end();
            it != end; ++it)
        {
            result.push_back(it->get());
        }
        return result;
    }

    void SetMeshData(const MeshHandle &handle, const void *vertexData, const void *indexData)
    {
        ComPtr<ID3D11DeviceContext> deviceContext;
        device_->GetImmediateContext(&deviceContext);

        meshManager_->SetData(device_, deviceContext.Get(), handle->index, vertexData, indexData);
    }

    void BeginRender(ID3D11DeviceContext *context, const FilterContext &filterContext)
    {
        deviceContext_ = context;
        filterContext_ = filterContext;
        currentDrawCall_ = 0;

        if (filterContext.options->statistics)
        {
            *filterContext.options->statistics = GeometryFX_FilterStatistics();
        }

        drawCommands_.clear();

        frameConstantBufferBackingStore_.view = filterContext.view;
        frameConstantBufferBackingStore_.projection = filterContext.projection;
        frameConstantBufferBackingStore_.height = filterContext.windowHeight;
        frameConstantBufferBackingStore_.width = filterContext.windowWidth;
        frameConstantBufferBackingStore_.cullFlags = filterContext.options->enabledFilters;

        D3D11_MAPPED_SUBRESOURCE mapping;
        context->Map(frameConstantBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapping);
        ::memcpy(mapping.pData, &frameConstantBufferBackingStore_,
            sizeof(frameConstantBufferBackingStore_));
        context->Unmap(frameConstantBuffer_.Get(), 0);

        context->PSSetShader(nullptr, NULL, 0);
    }

    void RenderMeshInstanced(
        const MeshHandle &handle, const int count, const DirectX::XMMATRIX *worldMatrices)
    {
        assert(deviceContext_);

        for (int i = 0; i < count; ++i)
        {
            DrawCommand request;
            request.mesh = handle->mesh;
            request.dcb.world = worldMatrices[i];
            request.dcb.worldView = worldMatrices[i] * filterContext_.view;
            request.dcb.meshIndex = handle->index;
            request.drawCallId = currentDrawCall_;

            if (!filterContext_.options->enableFiltering)
            {
                D3D11_MAPPED_SUBRESOURCE mapping;
                deviceContext_->Map(drawCallConstantBuffers_[currentDrawCall_].Get(), 0,
                    D3D11_MAP_WRITE_DISCARD, 0, &mapping);
                ::memcpy(mapping.pData, &request.dcb, sizeof(request.dcb));
                deviceContext_->Unmap(drawCallConstantBuffers_[currentDrawCall_].Get(), 0);
            }

            drawCommands_.push_back(request);

            ++currentDrawCall_;
        }
    }

    void EndRender()
    {
        // Set this up for all vertex shaders
        ID3D11Buffer *constantBuffers[] = { frameConstantBuffer_.Get() };

        deviceContext_->VSSetConstantBuffers(1, 1, constantBuffers);

        if (filterContext_.options->enableFiltering)
        {
            RenderGeometryChunked(deviceContext_, filterContext_);
        }
        else
        {
            RenderGeometryDefault(deviceContext_, filterContext_);
        }

        deviceContext_ = nullptr;
    }

    void GetBuffersForMesh(const MeshHandle &handle, ID3D11Buffer **vertexBuffer,
        int32 *vertexOffset, ID3D11Buffer **indexBuffer, int32 *indexOffset) const
    {
        const auto &mesh = handle->mesh;

        if (vertexBuffer)
        {
            *vertexBuffer = mesh->vertexBuffer.Get();
        }

        if (vertexOffset)
        {
            *vertexOffset = mesh->vertexOffset;
        }

        if (indexBuffer)
        {
            *indexBuffer = mesh->indexBuffer.Get();
        }

        if (indexOffset)
        {
            *indexOffset = mesh->indexOffset;
        }
    }

    void GetMeshInfo(const MeshHandle &handle, int32 *indexCount) const
    {
        if (indexCount)
        {
            *indexCount = handle->mesh->indexCount;
        }
    }

private:
    bool emulateMultiDrawIndirect_;
    AGSContext* agsContext_;

    std::vector<std::unique_ptr<GeometryFX_Filter::Handle>> handles_;

    std::unique_ptr<GeometryFX_Internal::IMeshManager> meshManager_;
    std::vector<ComPtr<ID3D11Buffer>> drawCallConstantBuffers_;
    int currentDrawCall_;
    int maxDrawCallCount_;

    std::vector<DrawCommand> drawCommands_;

    ID3D11DeviceContext *deviceContext_;
    FilterContext filterContext_;

    ComPtr<ID3D11Query> pipelineQuery_;
    ID3D11Device *device_;

    FrameConstantBuffer frameConstantBufferBackingStore_;
    ComPtr<ID3D11Buffer> frameConstantBuffer_;

    ComPtr<ID3D11ComputeShader> filterComputeShader_;

    std::vector<std::unique_ptr<SmallBatchChunk>> smallBatchChunks_;

    ComPtr<ID3D11ComputeShader> clearDrawIndirectArgumentsComputeShader_;

    ComPtr<ID3D11Buffer> indirectArgumentsBuffer_;
    ComPtr<ID3D11Buffer> indirectArgumentsBufferPristine_;
    ComPtr<ID3D11UnorderedAccessView> indirectArgumentsUAV_;

    ComPtr<ID3D11InputLayout> depthOnlyLayout_;
    ComPtr<ID3D11VertexShader> depthOnlyVertexShader_;
    ComPtr<ID3D11InputLayout> depthOnlyLayoutMID_;
    ComPtr<ID3D11VertexShader> depthOnlyVertexShaderMID_;

    void CreateShaders()
    {
        depthOnlyVertexShader_ = ComPtr<ID3D11VertexShader>();
        depthOnlyLayout_ = ComPtr<ID3D11InputLayout>();
        depthOnlyVertexShaderMID_ = ComPtr<ID3D11VertexShader>();
        depthOnlyLayoutMID_ = ComPtr<ID3D11InputLayout>();
        filterComputeShader_ = ComPtr<ID3D11ComputeShader>();
        clearDrawIndirectArgumentsComputeShader_ = ComPtr<ID3D11ComputeShader>();

        static const D3D11_INPUT_ELEMENT_DESC depthOnlyLayout[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };

        CreateShader(device_, (ID3D11DeviceChild **)depthOnlyVertexShader_.GetAddressOf(),
            sizeof(AMD_GeometryFX_DepthOnlyVS), AMD_GeometryFX_DepthOnlyVS, ShaderType::Vertex, &depthOnlyLayout_,
            ARRAYSIZE(depthOnlyLayout), depthOnlyLayout);

        static const D3D11_INPUT_ELEMENT_DESC depthOnlyLayoutMID[] =
        {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "DRAWID", 0, DXGI_FORMAT_R32_UINT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1 }
        };

        CreateShader(device_, (ID3D11DeviceChild **)depthOnlyVertexShaderMID_.GetAddressOf(),
            sizeof(AMD_GeometryFX_DepthOnlyMultiIndirectVS), AMD_GeometryFX_DepthOnlyMultiIndirectVS,
            ShaderType::Vertex, &depthOnlyLayoutMID_, ARRAYSIZE(depthOnlyLayoutMID),
            depthOnlyLayoutMID);

        CreateShader(device_,
            (ID3D11DeviceChild **)clearDrawIndirectArgumentsComputeShader_.GetAddressOf(),
            sizeof(AMD_GeometryFX_ClearDrawIndirectArgsCS), AMD_GeometryFX_ClearDrawIndirectArgsCS,
            ShaderType::Compute);

        CreateShader(device_, (ID3D11DeviceChild **)filterComputeShader_.GetAddressOf(),
            sizeof(AMD_GeometryFX_FilterCS), AMD_GeometryFX_FilterCS, ShaderType::Compute);
    }

    void CreateIndirectDrawArgumentsBuffer(
        const int meshCount, const int *indicesInMesh, const int * /* verticesInMesh */)
    {
        D3D11_BUFFER_DESC indirectArgsBufferDesc = {};
        indirectArgsBufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;

        const int roundedIndirectArgsCount = RoundToNextMultiple(meshCount, 256);

        // Round to multiples of 256 so the clear shader doesn't have to test
        // bounds
        indirectArgsBufferDesc.ByteWidth = sizeof(IndirectArguments) * roundedIndirectArgsCount;
        indirectArgsBufferDesc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
        indirectArgsBufferDesc.Usage = D3D11_USAGE_DEFAULT;
        indirectArgsBufferDesc.StructureByteStride = sizeof(IndirectArguments);

        std::vector<IndirectArguments> indirectArgs(roundedIndirectArgsCount);

        for (int i = 0; i < meshCount; ++i)
        {
            IndirectArguments::Init(indirectArgs[i]);
            indirectArgs[i].IndexCountPerInstance = indicesInMesh[i];
        }

        D3D11_SUBRESOURCE_DATA indirectArgsData;
        indirectArgsData.pSysMem = indirectArgs.data();
        indirectArgsData.SysMemPitch =
            static_cast<UINT>(indirectArgs.size() * sizeof(IndirectArguments));
        indirectArgsData.SysMemSlicePitch = indirectArgsData.SysMemPitch;

        device_->CreateBuffer(
            &indirectArgsBufferDesc, &indirectArgsData, &indirectArgumentsBuffer_);
        device_->CreateBuffer(
            &indirectArgsBufferDesc, &indirectArgsData, &indirectArgumentsBufferPristine_);

        SetDebugName(indirectArgumentsBuffer_.Get(), "[AMD GeometryFX Filtering] IndirectArgumentBuffer");
        SetDebugName(indirectArgumentsBufferPristine_.Get(),
            "[AMD GeometryFX Filtering] IndirectArgumentBuffer pristine version");

        D3D11_UNORDERED_ACCESS_VIEW_DESC indirectArgsUAVDesc = {};
        indirectArgsUAVDesc.Buffer.FirstElement = 0;
        indirectArgsUAVDesc.Buffer.NumElements = static_cast<int>(meshCount * 5);
        indirectArgsUAVDesc.Format = DXGI_FORMAT_R32_UINT;
        indirectArgsUAVDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;

        device_->CreateUnorderedAccessView(
            indirectArgumentsBuffer_.Get(), &indirectArgsUAVDesc, &indirectArgumentsUAV_);
    }

    void CreateDrawCallConstantBuffers()
    {
        drawCallConstantBuffers_.resize(maxDrawCallCount_);

        for (int i = 0; i < maxDrawCallCount_; ++i)
        {
            D3D11_BUFFER_DESC cbDesc;
            ZeroMemory(&cbDesc, sizeof(cbDesc));
            cbDesc.Usage = D3D11_USAGE_DYNAMIC;
            cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            cbDesc.ByteWidth = sizeof(DrawCallArguments);
            cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            device_->CreateBuffer(&cbDesc, NULL, &drawCallConstantBuffers_[i]);

            SetDebugName(
                drawCallConstantBuffers_[i].Get(), "[AMD GeometryFX Filtering] Draw call constant buffer [%d]", i);
        }
    }

    void CreateQueries()
    {
        D3D11_QUERY_DESC queryDesc;
        queryDesc.MiscFlags = 0;
        queryDesc.Query = D3D11_QUERY_PIPELINE_STATISTICS;
        device_->CreateQuery(&queryDesc, &pipelineQuery_);
    }

    void CreateConstantBuffers()
    {
        D3D11_BUFFER_DESC cbDesc;
        ZeroMemory(&cbDesc, sizeof(cbDesc));
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        cbDesc.ByteWidth = sizeof(FrameConstantBuffer);
        device_->CreateBuffer(&cbDesc, NULL, &frameConstantBuffer_);
        SetDebugName(frameConstantBuffer_.Get(), "[AMD GeometryFX Filtering] PerFrameConstantBuffer");
    }

    void ClearIndirectArgsBuffer(ID3D11DeviceContext *context) const
    {
        ID3D11UnorderedAccessView *uavViews[] = { indirectArgumentsUAV_.Get() };
        UINT initialCounts[] = { 0 };
        context->CSSetUnorderedAccessViews(1, 1, uavViews, initialCounts);
        context->CSSetShader(clearDrawIndirectArgumentsComputeShader_.Get(), nullptr, 0);
        context->Dispatch(
            static_cast<UINT>(RoundToNextMultiple(meshManager_->GetMeshCount(), 256)), 1, 1);

        uavViews[0] = nullptr;

        context->CSSetUnorderedAccessViews(0, 1, uavViews, initialCounts);
    }

    void RenderGeometryDefault(ID3D11DeviceContext *context, FilterContext & /* filterContext */) const
    {
        assert(context);
        ComPtr<ID3DUserDefinedAnnotation> annotation;
        context->QueryInterface(IID_PPV_ARGS(&annotation)); // QueryInterface can fail with E_NOINTERFACE

        context->IASetInputLayout(depthOnlyLayout_.Get());
		context->VSSetShader(depthOnlyVertexShader_.Get(), NULL, 0);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        if (annotation.Get() != nullptr)
        {
            annotation->BeginEvent(L"Depth pass");
        }

        for (std::vector<DrawCommand>::const_iterator it = drawCommands_.begin(),
            end = drawCommands_.end();
            it != end; ++it)
        {
            ID3D11Buffer *vertexBuffers[] = { it->mesh->vertexBuffer.Get() };
            UINT strides[] = { sizeof(float) * 3 };
            UINT offsets[] = { static_cast<UINT>(it->mesh->vertexOffset) };
            context->IASetVertexBuffers(0, 1, vertexBuffers, strides, offsets);
            context->IASetIndexBuffer(
                it->mesh->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, it->mesh->indexOffset);
            ID3D11Buffer *constantBuffers[] = { drawCallConstantBuffers_[it->drawCallId].Get() };
            context->VSSetConstantBuffers(0, 1, constantBuffers);
            context->DrawIndexed(it->mesh->indexCount, 0, 0);
        }

        if (annotation.Get() != nullptr)
        {
            annotation->EndEvent();
        }
    }

    void RenderGeometryChunked(ID3D11DeviceContext *context, FilterContext &filterContext) const
    {
        if (drawCommands_.empty())
        {
            return;
        }

        ComPtr<ID3DUserDefinedAnnotation> annotation;
        context->QueryInterface(IID_PPV_ARGS(&annotation)); // QueryInterface can fail with E_NOINTERFACE

        int currentSmallBatchChunk = 0;

        context->IASetInputLayout(depthOnlyLayoutMID_.Get());
        ID3D11VertexShader *vertexShader = depthOnlyVertexShaderMID_.Get();

        if (annotation.Get() != nullptr)
        {
            annotation->BeginEvent(L"Depth pass");
        }

        for (std::vector<DrawCommand>::const_iterator it = drawCommands_.begin(),
            end = drawCommands_.end();
            it != end; ++it)
        {
            DrawCommand current = *it;
            DrawCommand next;

            while (smallBatchChunks_[currentSmallBatchChunk]->AddRequest(current, next, filterContext))
            {
                const int trianglesInBatch =
                    smallBatchChunks_[currentSmallBatchChunk]->GetFaceCount();

                if (filterContext.options->statistics)
                {
                    filterContext.options->statistics->trianglesProcessed += trianglesInBatch;
                    context->Begin(pipelineQuery_.Get());
                }

                // Overflow, submit this batch and continue with next one
                smallBatchChunks_[currentSmallBatchChunk]->Render(context,
                    clearDrawIndirectArgumentsComputeShader_.Get(),
                    filterComputeShader_.Get(),
                    vertexShader, meshManager_->GetVertexBufferSRV(),
                    meshManager_->GetIndexBufferSRV(), meshManager_->GetMeshConstantsBuffer(),
                    meshManager_->GetVertexBuffer(), frameConstantBuffer_.Get());

                if (filterContext.options->statistics)
                {
                    context->End(pipelineQuery_.Get());

                    D3D11_QUERY_DATA_PIPELINE_STATISTICS stats;
                    while (context->GetData(pipelineQuery_.Get(), &stats, sizeof(stats), 0) != S_OK)
                    {
                        Yield();
                    }

                    filterContext.options->statistics->trianglesRendered += stats.IAPrimitives;
                    filterContext.options->statistics->trianglesCulled +=
                        (trianglesInBatch - stats.IAPrimitives);
                }

                current = next;
                currentSmallBatchChunk = (currentSmallBatchChunk + 1) % smallBatchChunks_.size();
            }
        }

        const int trianglesInBatch = smallBatchChunks_[currentSmallBatchChunk]->GetFaceCount();
        if (filterContext.options->statistics)
        {
            context->Begin(pipelineQuery_.Get());
            filterContext.options->statistics->trianglesProcessed += trianglesInBatch;
        }

        smallBatchChunks_[currentSmallBatchChunk]->Render(context,
            clearDrawIndirectArgumentsComputeShader_.Get(),
            filterComputeShader_.Get(),
            vertexShader, meshManager_->GetVertexBufferSRV(), meshManager_->GetIndexBufferSRV(),
            meshManager_->GetMeshConstantsBuffer(), meshManager_->GetVertexBuffer(),
            frameConstantBuffer_.Get());

        if (filterContext.options->statistics)
        {
            context->End(pipelineQuery_.Get());

            D3D11_QUERY_DATA_PIPELINE_STATISTICS stats;
            while (context->GetData(pipelineQuery_.Get(), &stats, sizeof(stats), 0) != S_OK)
            {
                Yield();
            }

            filterContext.options->statistics->trianglesRendered += stats.IAPrimitives;
            filterContext.options->statistics->trianglesCulled +=
                (trianglesInBatch - stats.IAPrimitives);
        }

        if (annotation.Get() != nullptr)
        {
            annotation->EndEvent();
        }
    }
};

///////////////////////////////////////////////////////////////////////////////
GeometryFX_Filter::GeometryFX_Filter(const GeometryFX_FilterDesc *pDesc)
{
    const GeometryFX_FilterDesc desc = pDesc ? *pDesc : GeometryFX_FilterDesc();

    impl_ = new GeometryFX_OpaqueFilterDesc(desc);
}

///////////////////////////////////////////////////////////////////////////////
GeometryFX_Filter::~GeometryFX_Filter()
{
    delete impl_;
}

///////////////////////////////////////////////////////////////////////////////
std::vector<GeometryFX_Filter::MeshHandle> GeometryFX_Filter::RegisterMeshes(
    const int meshCount, const int *verticesInMesh, const int *indicesInMesh)
{
    assert(meshCount > 0);
    assert(verticesInMesh != nullptr);
    assert(indicesInMesh != nullptr);

    return impl_->RegisterMeshes(meshCount, verticesInMesh, indicesInMesh);
}

///////////////////////////////////////////////////////////////////////////////
void GeometryFX_Filter::SetMeshData(const GeometryFX_Filter::MeshHandle &handle, const void *vertexData, const void *indexData)
{
    assert(vertexData != nullptr);
    assert(indexData != nullptr);

    impl_->SetMeshData(handle, vertexData, indexData);
}

///////////////////////////////////////////////////////////////////////////////
void GeometryFX_Filter::BeginRender(ID3D11DeviceContext *context, const GeometryFX_FilterRenderOptions &options,
    const DirectX::XMMATRIX &view, const DirectX::XMMATRIX &projection, const int windowWidth,
    const int windowHeight)
{
    assert(context != nullptr);
    assert(windowWidth > 0);
    assert(windowHeight > 0);

    FilterContext filterContext;
    filterContext.options = &options;
    filterContext.projection = projection;
    filterContext.view = view;
    filterContext.windowWidth = windowWidth;
    filterContext.windowHeight = windowHeight;

    const auto inverseView = DirectX::XMMatrixInverse (nullptr, view);
    DirectX::XMFLOAT4X4 float4x4;
    XMStoreFloat4x4 (&float4x4, inverseView);

    filterContext.eye = DirectX::XMVectorSet (float4x4._41, float4x4._42, float4x4._43, 1);

    impl_->BeginRender(context, filterContext);
}

///////////////////////////////////////////////////////////////////////////////
void GeometryFX_Filter::RenderMesh(const GeometryFX_Filter::MeshHandle &handle, const DirectX::XMMATRIX &world)
{
    RenderMeshInstanced(handle, 1, &world);
}

///////////////////////////////////////////////////////////////////////////////
void GeometryFX_Filter::RenderMeshInstanced(
    const GeometryFX_Filter::MeshHandle &handle, const int instanceCount, const DirectX::XMMATRIX *worlds)
{
    impl_->RenderMeshInstanced(handle, instanceCount, worlds);
}

///////////////////////////////////////////////////////////////////////////////
void GeometryFX_Filter::EndRender()
{
    impl_->EndRender();
}

///////////////////////////////////////////////////////////////////////////////
void GeometryFX_Filter::GetBuffersForMesh(const MeshHandle &handle, ID3D11Buffer **vertexBuffer,
    int32 *vertexOffset, ID3D11Buffer **indexBuffer, int32 *indexOffset) const
{
    impl_->GetBuffersForMesh(handle, vertexBuffer, vertexOffset, indexBuffer, indexOffset);
}

///////////////////////////////////////////////////////////////////////////////
void GeometryFX_Filter::GetMeshInfo(const MeshHandle &handle, int32 *indexCount) const
{
    impl_->GetMeshInfo(handle, indexCount);
}

} // namespace AMD
