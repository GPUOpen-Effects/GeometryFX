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

#include "AMD_GeometryFX_Utility.h"

#include <cstdio>

#pragma warning(disable : 4996)

namespace AMD
{
////////////////////////////////////////////////////////////////////////////////
GEOMETRYFX_RETURN_CODE GeometryFX_GetVersion(uint* major, uint* minor, uint* patch)
{
    if (major == NULL || minor == NULL || patch == NULL)
    {
        return GEOMETRYFX_RETURN_CODE_INVALID_POINTER;
    }

    *major = AMD_GEOMETRYFX_VERSION_MAJOR;
    *minor = AMD_GEOMETRYFX_VERSION_MINOR;
    *patch = AMD_GEOMETRYFX_VERSION_PATCH;

    return GEOMETRYFX_RETURN_CODE_SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
void GeometryFX_WriteBlobToFile(const char *filename, const std::size_t size, const void *data)
{
    auto handle = std::fopen(filename, "wb");
    std::fwrite(data, size, 1, handle);
    std::fclose(handle);
}

////////////////////////////////////////////////////////////////////////////////
std::vector<byte> GeometryFX_ReadBlobFromFile(const char *filename)
{
    std::vector<byte> result;
    byte buffer[4096];

    auto handle = std::fopen(filename, "rb");

    for (;;)
    {
        const auto bytesRead = std::fread(buffer, 1, sizeof(buffer), handle);

        result.insert(result.end(), buffer, buffer + bytesRead);

        if (bytesRead < sizeof(buffer))
        {
            break;
        }
    }

    std::fclose(handle);

    return result;
}

} // namespace AMD
