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

#include "GeometryFXMeshManager.h"

#include "GeometryFXMesh.h"
#include "GeometryFXUtility_Internal.h"
#include "AMD_GeometryFX_Internal.h"

#include <wrl.h>

#include <memory>
#include <vector>
#include <array>

#include <DirectXMath.h>

#define AMD_GEOMETRY_FX_ENABLE_CLUSTER_CENTER_SAFETY_CHECK 1

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

using namespace Microsoft::WRL;

namespace AMD
{
namespace GeometryFX_Internal
{
///////////////////////////////////////////////////////////////////////////////
IMeshManager::IMeshManager()
{
}

///////////////////////////////////////////////////////////////////////////////
IMeshManager::~IMeshManager()
{
}

///////////////////////////////////////////////////////////////////////////////
class MeshManagerBase : public IMeshManager
{
  public:
    StaticMesh *GetMesh(const int index) const override
    {
        return meshes_[index].get();
    }

    int GetMeshCount() const override
    {
        return static_cast<int>(meshes_.size());
    }

    ID3D11ShaderResourceView *GetMeshConstantsBuffer() const override
    {
        return meshConstantsBufferView_.Get();
    }

  protected:
    std::vector<std::unique_ptr<StaticMesh>> meshes_;
    ComPtr<ID3D11Buffer> meshConstantsBuffer_;
    ComPtr<ID3D11ShaderResourceView> meshConstantsBufferView_;

    void CreateMeshConstantsBuffer(ID3D11Device *device)
    {
        std::vector<MeshConstants> meshConstants(GetMeshCount());

        for (int i = 0; i < GetMeshCount(); ++i)
        {
            meshConstants[i].faceCount = meshes_[i]->faceCount;
            meshConstants[i].indexOffset = meshes_[i]->indexOffset;
            meshConstants[i].vertexCount = meshes_[i]->vertexCount;
            meshConstants[i].vertexOffset = meshes_[i]->vertexOffset;
        }

        D3D11_BUFFER_DESC bufferDesc = {};
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.ByteWidth = static_cast<UINT>(meshConstants.size() * sizeof(MeshConstants));
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = sizeof(MeshConstants);
        bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;

        D3D11_SUBRESOURCE_DATA initialData;
        initialData.pSysMem = meshConstants.data();
        initialData.SysMemPitch = bufferDesc.ByteWidth;
        initialData.SysMemSlicePitch = bufferDesc.ByteWidth;

        device->CreateBuffer(&bufferDesc, &initialData, &meshConstantsBuffer_);

        SetDebugName(meshConstantsBuffer_.Get(), "Mesh constants buffer");

        for (std::vector<std::unique_ptr<StaticMesh>>::iterator it = meshes_.begin(),
                                                                end = meshes_.end();
             it != end; ++it)
        {
            (*it)->meshConstantsBuffer = meshConstantsBuffer_;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.ElementOffset = 0;
        srvDesc.Buffer.ElementWidth = GetMeshCount();
        device->CreateShaderResourceView(
            meshConstantsBuffer_.Get(), &srvDesc, &meshConstantsBufferView_);

        SetDebugName(meshConstantsBufferView_.Get(), "Mesh constants buffer view");
    }
};

///////////////////////////////////////////////////////////////////////////////
// Allocate everything from one large buffer
class MeshManagerGlobal : public MeshManagerBase
{
public:
    void Allocate(ID3D11Device *device, const int meshCount, const int *verticesPerMesh,
        const int *indicesPerMesh) override
    {
        int totalVertexCount = 0;
        int totalIndexCount = 0;

        for (int i = 0; i < meshCount; ++i)
        {
            totalVertexCount += verticesPerMesh[i];
            totalIndexCount += indicesPerMesh[i];
        }

        CreateVertexBuffer(device, totalVertexCount);
        CreateIndexBuffer(device, totalIndexCount);

        int indexOffset = 0;
        int vertexOffset = 0;
        for (int i = 0; i < meshCount; ++i)
        {
            meshes_.emplace_back(std::unique_ptr<StaticMesh>(
                new StaticMesh(verticesPerMesh[i], indicesPerMesh[i], i)));
            meshes_[i]->vertexBuffer = vertexBuffer_;
            meshes_[i]->vertexBufferSRV = vertexBufferSRV_;
            meshes_[i]->indexBuffer = indexBuffer_;
            meshes_[i]->indexBufferSRV = indexBufferSRV_;

            meshes_[i]->indexOffset = indexOffset;
            indexOffset += indicesPerMesh[i] * sizeof(int);

            meshes_[i]->vertexOffset = vertexOffset;
            vertexOffset += verticesPerMesh[i] * 3 * sizeof(float);
        }

        CreateMeshConstantsBuffer(device);
    }

    std::vector<StaticMesh::Cluster> CreateClusters (
        const int indexCount,
        const void* vertexData,
        const void* indexData)
    {
        const int32_t* indices = static_cast<const int32_t*> (indexData);
        const float* vertices = static_cast<const float*> (vertexData);

        // 16 KiB stack space
        struct Triangle
        {
            DirectX::XMVECTOR vtx[3];
        };

        std::array<Triangle, SmallBatchMergeConstants::BATCH_SIZE * 3> triangleCache;

        const int triangleCount = indexCount / 3;
        const int clusterCount = (triangleCount + SmallBatchMergeConstants::BATCH_SIZE - 1)
            / SmallBatchMergeConstants::BATCH_SIZE;

        std::vector<StaticMesh::Cluster> result (clusterCount);
        for (int i = 0; i < clusterCount; ++i)
        {
            const int clusterStart = i * SmallBatchMergeConstants::BATCH_SIZE;
            const int clusterEnd = std::min (clusterStart + SmallBatchMergeConstants::BATCH_SIZE,
                triangleCount);

            const int clusterTriangleCount = clusterEnd - clusterStart;

            // Load all triangles into our local cache
            for (int triangleIndex = clusterStart; triangleIndex < clusterEnd; ++triangleIndex)
            {
                triangleCache[triangleIndex - clusterStart].vtx[0] = DirectX::XMVectorSet (
                    vertices[indices[triangleIndex * 3 + 0] * 3 + 0],
                    vertices[indices[triangleIndex * 3 + 0] * 3 + 1],
                    vertices[indices[triangleIndex * 3 + 0] * 3 + 2],
                    1.0f
                );

                triangleCache[triangleIndex - clusterStart].vtx[1] = DirectX::XMVectorSet (
                    vertices[indices[triangleIndex * 3 + 1] * 3 + 0],
                    vertices[indices[triangleIndex * 3 + 1] * 3 + 1],
                    vertices[indices[triangleIndex * 3 + 1] * 3 + 2],
                    1.0f
                );

                triangleCache[triangleIndex - clusterStart].vtx[2] = DirectX::XMVectorSet (
                    vertices[indices[triangleIndex * 3 + 2] * 3 + 0],
                    vertices[indices[triangleIndex * 3 + 2] * 3 + 1],
                    vertices[indices[triangleIndex * 3 + 2] * 3 + 2],
                    1.0f
                );
            }

            auto aabbMin = DirectX::XMVectorSplatInfinity ();
            auto aabbMax = DirectX::XMVectorNegate (DirectX::XMVectorSplatInfinity ());

            auto coneAxis = DirectX::XMVectorZero ();

            for (int triangleIndex = 0; triangleIndex < clusterTriangleCount; ++triangleIndex)
            {
                const auto& triangle = triangleCache[triangleIndex];
                for (int j = 0; j < 3; ++j)
                {
                    aabbMin = DirectX::XMVectorMin (aabbMin, triangle.vtx[j]);
                    aabbMax = DirectX::XMVectorMax (aabbMax, triangle.vtx[j]);
                }
                
                const auto triangleNormal = DirectX::XMVector3Normalize (
                    DirectX::XMVector3Cross (
                        DirectX::XMVectorSubtract (triangle.vtx[1], triangle.vtx[0]),
                        DirectX::XMVectorSubtract( triangle.vtx[2], triangle.vtx[0])));

                coneAxis = DirectX::XMVectorAdd (coneAxis, DirectX::XMVectorNegate (triangleNormal));
            }

            // This is the cosine of the cone opening angle - 1 means it's 0°,
            // we're minimizing this value (at 0, it would mean the cone is 90°
            // open)
            float coneOpening = 1;
            bool validCluster = true;

            const auto center = DirectX::XMVectorDivide (DirectX::XMVectorAdd (aabbMin, aabbMax),
                DirectX::XMVectorSet (2, 2, 2, 2));
            coneAxis = DirectX::XMVector3Normalize (coneAxis);

            float t = -std::numeric_limits<float>::infinity ();

            // We nee a second pass to find the intersection of the line
            // center + t * coneAxis with the plane defined by each
            // triangle
            for (int triangleIndex = 0; triangleIndex < clusterTriangleCount; ++triangleIndex)
            {
                const auto& triangle = triangleCache[triangleIndex];
                // Compute the triangle plane from the three vertices
                
                const auto triangleNormal = DirectX::XMVector3Normalize (
                    DirectX::XMVector3Cross (
                        DirectX::XMVectorSubtract (triangle.vtx[1], triangle.vtx[0]),
                        DirectX::XMVectorSubtract (triangle.vtx[2], triangle.vtx[0])));

                const float directionalPart = DirectX::XMVectorGetX (
                    DirectX::XMVector3Dot (coneAxis, DirectX::XMVectorNegate (triangleNormal)));

                if (directionalPart < 0)
                {
                    // No solution for this cluster - at least two triangles
                    // are facing each other
                    validCluster = false;
                    break;
                }

                // We need to intersect the plane with our cone ray which is
                // center + t * coneAxis, and find the max
                // t along the cone ray (which points into the empty
                // space)
                // See: https://en.wikipedia.org/wiki/Line%E2%80%93plane_intersection
                const float td = DirectX::XMVectorGetX (DirectX::XMVectorDivide (
                    DirectX::XMVector3Dot (DirectX::XMVectorSubtract(center, triangle.vtx[0]), triangleNormal),
                    DirectX::XMVectorSet (-directionalPart, -directionalPart, -directionalPart, -directionalPart)));

                t = std::max (t, td);

                coneOpening = std::min (coneOpening, directionalPart);
            }

            result[i].aabbMax = aabbMax;
            result[i].aabbMin = aabbMin;

            // cos (PI/2 - acos (coneOpening))
            result[i].coneAngleCosine = sqrtf (1 - coneOpening * coneOpening);
            result[i].coneCenter = DirectX::XMVectorAdd (center,
                DirectX::XMVectorMultiply (coneAxis, DirectX::XMVectorSet (t, t, t, t)));
            result[i].coneAxis = coneAxis;

#if AMD_GEOMETRY_FX_ENABLE_CLUSTER_CENTER_SAFETY_CHECK
            // If distance of coneCenter to the bounding box center is more
            // than 16x the bounding box extent, the cluster is also invalid
            // This is mostly a safety measure - if triangles are nearly
            // parallel to coneAxis, t may become very large and unstable
            const float aabbSize = DirectX::XMVectorGetX (DirectX::XMVector3Length (DirectX::XMVectorSubtract (aabbMax, aabbMin)));
            const float coneCenterToCenterDistance = DirectX::XMVectorGetX (
                DirectX::XMVector3Length (
                    DirectX::XMVectorSubtract (result[i].coneCenter,
                        DirectX::XMVectorDivide (
                            DirectX::XMVectorAdd (aabbMax, aabbMin),
                            DirectX::XMVectorSet (2, 2, 2, 2))
            )));

            if (coneCenterToCenterDistance > (16 * aabbSize))
            {
                validCluster = false;
            }
#endif

            result[i].valid = validCluster;
        }

        return result;
    }

    void SetData(ID3D11Device * /* device */, ID3D11DeviceContext *context, const int meshIndex,
        const void *vertexData, const void *indexData) override
    {
        D3D11_BOX dstBox;
        dstBox.left = meshes_[meshIndex]->vertexOffset;
        dstBox.right = dstBox.left + meshes_[meshIndex]->vertexCount * 3 * sizeof(float);
        dstBox.top = 0;
        dstBox.bottom = 1;
        dstBox.front = 0;
        dstBox.back = 1;
        context->UpdateSubresource(vertexBuffer_.Get(), 0, &dstBox, vertexData, 0, 0);

        dstBox.left = meshes_[meshIndex]->indexOffset;
        dstBox.right = dstBox.left + meshes_[meshIndex]->indexCount * sizeof(int);
        context->UpdateSubresource(indexBuffer_.Get(), 0, &dstBox, indexData, 0, 0);

        meshes_[meshIndex]->clusters = CreateClusters (meshes_[meshIndex]->indexCount,
            vertexData, indexData);
    }

  private:
    void CreateVertexBuffer(ID3D11Device *device, const int vertexCount)
    {
        D3D11_BUFFER_DESC vbDesc = {};
        vbDesc.Usage = D3D11_USAGE_DEFAULT;
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_SHADER_RESOURCE;
        vbDesc.ByteWidth = sizeof(float) * 3 * vertexCount;
        vbDesc.StructureByteStride = 0;
        vbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;

        device->CreateBuffer(&vbDesc, nullptr, &vertexBuffer_);
        SetDebugName(vertexBuffer_.Get(), "Global source vertex buffer");

        D3D11_SHADER_RESOURCE_VIEW_DESC vbSrv;
        vbSrv.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
        vbSrv.BufferEx.FirstElement = 0;
        vbSrv.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
        vbSrv.BufferEx.NumElements = vbDesc.ByteWidth / 4;
        vbSrv.Format = DXGI_FORMAT_R32_TYPELESS;

        device->CreateShaderResourceView(vertexBuffer_.Get(), &vbSrv, &vertexBufferSRV_);
        SetDebugName(vertexBufferSRV_.Get(), "Global source vertex buffer resource view");
    }

    void CreateIndexBuffer(ID3D11Device *device, const int indexCount)
    {
        D3D11_BUFFER_DESC ibDesc = {};
        ibDesc.Usage = D3D11_USAGE_DEFAULT;
        ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER | D3D11_BIND_SHADER_RESOURCE;
        ibDesc.ByteWidth = indexCount * sizeof(int);
        ibDesc.StructureByteStride = sizeof(int);

        device->CreateBuffer(&ibDesc, nullptr, &indexBuffer_);
        SetDebugName(indexBuffer_.Get(), "Global index buffer");

        D3D11_SHADER_RESOURCE_VIEW_DESC ibSrv;
        ibSrv.Format = DXGI_FORMAT_R32_UINT;
        ibSrv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        ibSrv.Buffer.ElementOffset = 0;
        ibSrv.Buffer.ElementWidth = sizeof(int);
        ibSrv.Buffer.FirstElement = 0;
        ibSrv.Buffer.NumElements = static_cast<UINT>(indexCount);

        device->CreateShaderResourceView(indexBuffer_.Get(), &ibSrv, &indexBufferSRV_);
        SetDebugName(indexBufferSRV_.Get(), "Global source index buffer view");
    }

  public:
    ID3D11Buffer *GetIndexBuffer() const
    {
        return indexBuffer_.Get();
    }

    ID3D11Buffer *GetVertexBuffer() const
    {
        return vertexBuffer_.Get();
    }

    ID3D11ShaderResourceView *GetIndexBufferSRV() const
    {
        return indexBufferSRV_.Get();
    }

    ID3D11ShaderResourceView *GetVertexBufferSRV() const
    {
        return vertexBufferSRV_.Get();
    }

  private:
    ComPtr<ID3D11Buffer> vertexBuffer_;
    ComPtr<ID3D11ShaderResourceView> vertexBufferSRV_;
    ComPtr<ID3D11Buffer> indexBuffer_;
    ComPtr<ID3D11ShaderResourceView> indexBufferSRV_;
};

///////////////////////////////////////////////////////////////////////////////
std::unique_ptr<IMeshManager> CreateGlobalMeshManager()
{
    return std::unique_ptr<IMeshManager>(new MeshManagerGlobal());
}

} // namespace GeometryFX_Internal
} // namespace AMD
