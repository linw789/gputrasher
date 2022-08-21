// Copyright (c) 2022 Advanced Micro Devices, Inc. All rights reserved.

#include "utils.h"
#include <d3d12.h>

using namespace Microsoft::WRL;

HRESULT FindD3D12HardwareAdapter(
    ComPtr<IDXGIFactory4> factory,
    ComPtr<IDXGIAdapter1> outAdapter)
{
    HRESULT hr = E_NOINTERFACE;

    ComPtr<IDXGIAdapter1> adapter;

    for (UINT adapterIdx = 0;
        SUCCEEDED(factory->EnumAdapters1(adapterIdx, &adapter));
        ++adapterIdx)
    {
        DXGI_ADAPTER_DESC1 desc = {};
        adapter->GetDesc1(&desc);

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        {
            // Skip software adapter.
            continue;
        }

        // Check D3D12 support without creating a device.
        hr = D3D12CreateDevice(
            adapter.Get(),
            D3D_FEATURE_LEVEL_12_0,
            _uuidof(ID3D12Device),
            nullptr);

        if (SUCCEEDED(hr))
        {
            outAdapter = adapter.Detach();
            hr = S_OK;
            break;
        }
    }

    return hr;
}