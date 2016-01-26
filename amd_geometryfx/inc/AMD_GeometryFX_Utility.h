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

#ifndef __AMD_GEOMETRYFX_UTILITY_H__
#define __AMD_GEOMETRYFX_UTILITY_H__

#include <d3d11.h>
#include <vector>

#include <varargs.h>

#include "AMD_GeometryFX.h"

namespace AMD
{
template <typename T> T RoundToNextMultiple(T value, T multiple)
{
	return ((value + multiple - 1) / multiple) * multiple;
}

template <typename U, typename T> bool TestFlag(const U m, T i)
{
	return (m & static_cast<U>(i)) == static_cast<U>(i);
}

template <typename U, typename T> U SetOrClearFlag(U &m, const T i, const bool set)
{
	if (set)
	{
		m |= static_cast<U>(i);
	}
	else
	{
		m &= ~static_cast<U>(i);
	}

	return m;
}

AMD_GEOMETRYFX_DLL_API void WriteBlobToFile(const char *filename, const std::size_t size, const void *data);
AMD_GEOMETRYFX_DLL_API std::vector<byte> ReadBlobFromFile(const char *filename);

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

AMD_GEOMETRYFX_DLL_API bool CreateShader(ID3D11Device *device, ID3D11DeviceChild **ppShader, const std::size_t shaderSize,
	const void *shaderSource, ShaderType::Enum shaderType,
	ID3D11InputLayout **inputLayout = nullptr, const int inputElementCount = 0,
	const D3D11_INPUT_ELEMENT_DESC *inputElements = nullptr);
}

#endif // __AMD_GEOMETRYFX_UTILITY_H__