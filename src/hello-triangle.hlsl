// Copyright (c) 2022 Advanced Micro Devices, Inc. All rights reserved.

cbuffer ConstBuffer : register(b0)
{
    // Data field in constant buffer is 256-byte aligned. And the maximum size
    // is 65535. So the maximum count of float4 allowed is 4096.
    float4 colors[4096];
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

PSInput VSMain(float4 position : POSITION, float4 color : COLOR)
{
    PSInput result;

    result.position = position;
    result.color = color;
    int colorIndex = (int)colors[0].x;
    result.color = colors[colorIndex];

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return input.color;
}
