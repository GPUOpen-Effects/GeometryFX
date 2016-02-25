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

#include "GeometryFXUtility_Internal.h"

namespace AMD
{
namespace GeometryFX_Internal
{

////////////////////////////////////////////////////////////////////////////////
bool CreateShader(ID3D11Device *device, ID3D11DeviceChild **shader, const size_t shaderSize,
    const void *shaderSource, ShaderType::Enum shaderType, ID3D11InputLayout **inputLayout,
    const int inputElementCount, const D3D11_INPUT_ELEMENT_DESC *inputElements)
{
    if (inputLayout)
    {
        device->CreateInputLayout(
            inputElements, inputElementCount, shaderSource, shaderSize, inputLayout);
    }

    switch (shaderType)
    {
        case ShaderType::Compute:
            device->CreateComputeShader(
                shaderSource, shaderSize, nullptr, (ID3D11ComputeShader **)shader);
            break;
        case ShaderType::Pixel:
            device->CreatePixelShader(
                shaderSource, shaderSize, nullptr, (ID3D11PixelShader **)shader);
            break;
        case ShaderType::Vertex:
            device->CreateVertexShader(
                shaderSource, shaderSize, nullptr, (ID3D11VertexShader **)shader);
            break;
        case ShaderType::Hull:
            device->CreateHullShader(
                shaderSource, shaderSize, nullptr, (ID3D11HullShader **)shader);
            break;
        case ShaderType::Domain:
            device->CreateDomainShader(
                shaderSource, shaderSize, nullptr, (ID3D11DomainShader **)shader);
            break;
        case ShaderType::Geometry:
            device->CreateGeometryShader(
                shaderSource, shaderSize, nullptr, (ID3D11GeometryShader **)shader);
            break;
    }

    return true;
}

} // namespace GeometryFX_Internal
} // namespace AMD
