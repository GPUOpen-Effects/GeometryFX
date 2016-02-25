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

float4 FullscreenVS(uint id : SV_VERTEXID) : SV_POSITION
{
    float x = 1.0 - ((id & 2) << 1);
    float y = 1.0 - ((id & 1) << 2);

    return float4 (x, y, 0, 1);
}

cbuffer FullscreenConstantBuffer     : register(b0)
{
    uint    windowWidth;
    uint    windowHeight;
    uint    shadowMapWidth;
    uint    shadowMapHeight;
};

uint2 tea (uint2 v)
{
    uint sum=0, delta=0x9e3779b9;
    uint k[4] = { 0xA341316C, 0xC8013EA4, 0xAD90777D, 0x7E95761E };
    for (uint i = 0; i < 5; ++i)
    {
        sum += delta;
        v.x += ((v.y << 4)+k[0] )^(v.y + sum)^((v.y >> 5)+k[1] );
        v.y += ((v.x << 4)+k[2] )^(v.x + sum)^((v.x >> 5)+k[3] );
    }
    return v;
}

Texture2D    depthMap                                       : register(t0);
float4 FullscreenPS(float4 pos : SV_POSITION) : SV_Target
{
    // shadow map pixels per output window pixels
    float dx = float (shadowMapWidth) / float (windowWidth);
    float dy = float (shadowMapHeight) / float (windowHeight);

    float scale = max (dx, dy);
    int scaledWidth = shadowMapWidth / scale;
    int scaledHeight = shadowMapHeight / scale;

    int2 windowCenter = int2 (windowWidth, windowHeight) / 2;
    int2 shadowMapCenter = int2 (scaledWidth, scaledHeight) / 2;
    int2 centerDelta = shadowMapCenter - windowCenter;

    // Jitter inside the pixel footprint
    float sx = 0, sy = 0;

    uint2 screenSpacePos = uint2 (pos.xy) + centerDelta;

    uint2 r = tea (screenSpacePos);
    float2 rf = float2 (r) / 4294967295.0f;

    sx = scale * rf.x;
    sy = scale * rf.y;

    // Center shadow map inside window

    // snap to correct pixel
    int px = int (screenSpacePos.x * scale + sx);
    int py = int (screenSpacePos.y * scale + sy);

    if (px >= shadowMapWidth || py >= shadowMapHeight || px < 0 || py < 0)
    {
        // Out of bounds
        bool stripeIn = uint((pos.x + rf.x * 4 + pos.y + rf.y * 4) / 16) & 2;
        return float4 ((stripeIn ? 0.75 : 0.4) * (1.0f - 0.3f * rf.x), 0, 0, 1);
    }

    float d = depthMap.Load (int3 (px, py, 0)).x;
    return float4 (saturate(1.0 - d + rf.x / 256.0f).rrr, 1);
}
