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

#ifndef AMD_GEOMETRYFX_H
#define AMD_GEOMETRYFX_H

#   define AMD_GEOMETRYFX_VERSION_MAJOR             1
#   define AMD_GEOMETRYFX_VERSION_MINOR             2
#   define AMD_GEOMETRYFX_VERSION_PATCH             0

// default to static lib
#   ifndef AMD_GEOMETRYFX_COMPILE_DYNAMIC_LIB
#       define AMD_GEOMETRYFX_COMPILE_DYNAMIC_LIB   0
#   endif

#   if AMD_GEOMETRYFX_COMPILE_DYNAMIC_LIB
#       ifdef AMD_DLL_EXPORTS
#           define AMD_GEOMETRYFX_DLL_API __declspec(dllexport)
#       else // AMD_DLL_EXPORTS
#           define AMD_GEOMETRYFX_DLL_API __declspec(dllimport)
#       endif // AMD_DLL_EXPORTS
#   else // AMD_GEOMETRYFX_COMPILE_DYNAMIC_LIB
#       define AMD_GEOMETRYFX_DLL_API
#   endif // AMD_GEOMETRYFX_COMPILE_DYNAMIC_LIB

#include "AMD_Types.h"

#   if defined(DEBUG) || defined(_DEBUG)
#       define AMD_GEOMETRYFX_DEBUG                 1
#   endif

namespace AMD
{
    // The Return codes
    typedef enum GEOMETRYFX_RETURN_CODE_t
    {
        GEOMETRYFX_RETURN_CODE_SUCCESS,
        GEOMETRYFX_RETURN_CODE_FAIL,
        GEOMETRYFX_RETURN_CODE_INVALID_ARGUMENT,
        GEOMETRYFX_RETURN_CODE_INVALID_POINTER,
        GEOMETRYFX_RETURN_CODE_D3D11_CALL_FAILED,

        GEOMETRYFX_RETURN_CODE_COUNT,
    } GEOMETRYFX_RETURN_CODE;
}

#endif // AMD_GEOMETRYFX_H
