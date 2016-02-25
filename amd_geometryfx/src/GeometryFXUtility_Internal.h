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

#ifndef AMD_GEOMETRYFX_UTILITY_INTERNAL_H
#define AMD_GEOMETRYFX_UTILITY_INTERNAL_H

#include <d3d11.h>

#include <varargs.h>

#include "AMD_GeometryFX.h"

namespace AMD
{
namespace GeometryFX_Internal
{

template <typename T> T RoundToNextMultiple(T value, T multiple)
{
    return ((value + multiple - 1) / multiple) * multiple;
}

template <typename T> void SetDebugName(T pObject, const char *s, ...)
{
    char buffer[512] = {};
    va_list args;
    va_start(args, s);
    vsprintf_s(buffer, s, args);
    va_end(args);
    pObject->SetPrivateData(WKPDID_D3DDebugObjectName,
        static_cast<UINT> (::strlen (buffer) - 1), buffer);
}

struct ShaderType
{
    enum Enum
    {
        Vertex,
        Domain,
        Hull,
        Geometry,
        Pixel,
        Compute
    };
};

bool CreateShader(ID3D11Device *device, ID3D11DeviceChild **ppShader, const size_t shaderSize,
    const void *shaderSource, ShaderType::Enum shaderType,
    ID3D11InputLayout **inputLayout = nullptr, const int inputElementCount = 0,
    const D3D11_INPUT_ELEMENT_DESC *inputElements = nullptr);

} // namespace GeometryFX_Internal
} // namespace AMD

#endif // AMD_GEOMETRYFX_UTILITY_INTERNAL_H
