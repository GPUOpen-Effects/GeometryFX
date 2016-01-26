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

#ifndef AMD_GEOMETRYFX_INTERNAL_MESHMANAGER_H_71A7953FF8E74B84A48F7EA45E2145D2
#define AMD_GEOMETRYFX_INTERNAL_MESHMANAGER_H_71A7953FF8E74B84A48F7EA45E2145D2

#include <d3d11.h>
#include <wrl.h>
#include <memory>
#include "AMD_Types.h"

namespace AMD
{
namespace GeometryFX_Internal
{
class StaticMesh;

#pragma pack(push, 1)
struct MeshConstants
{
    uint32 vertexCount;
    uint32 faceCount;
    uint32 indexOffset;
    uint32 vertexOffset;
};
#pragma pack(pop)

class IMeshManager
{
  public:
	IMeshManager ();
    virtual ~IMeshManager();

    virtual void Allocate(ID3D11Device *pDevice, const int meshCount, const int *verticesPerMesh,
        const int *indicesPerMesh) = 0;

    virtual void SetData(ID3D11Device *pDevice, ID3D11DeviceContext *pContext, const int meshIndex,
        const void *pVertexData, const void *pIndexData) = 0;

    virtual StaticMesh *GetMesh(const int index) const = 0;
    virtual int GetMeshCount() const = 0;

    virtual ID3D11ShaderResourceView *GetMeshConstantsBuffer() const = 0;

	virtual ID3D11Buffer *GetIndexBuffer () const = 0;
	virtual ID3D11Buffer *GetVertexBuffer () const = 0;
	virtual ID3D11ShaderResourceView *GetIndexBufferSRV () const = 0;
	virtual ID3D11ShaderResourceView *GetVertexBufferSRV () const = 0;

  private:
    IMeshManager(const IMeshManager &);
    IMeshManager &operator=(const IMeshManager &);
};

std::unique_ptr<IMeshManager> CreateGlobalMeshManager();
}
}

#endif