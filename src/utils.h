// Copyright (c) 2022 Advanced Micro Devices, Inc. All rights reserved.

#include <wrl/client.h>
#include <dxgi1_6.h>

HRESULT FindD3D12HardwareAdapter(
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory,
    Microsoft::WRL::ComPtr<IDXGIAdapter1> outAdapter);
