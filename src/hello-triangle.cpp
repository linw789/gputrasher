// Copyright (c) 2022 Advanced Micro Devices, Inc. All rights reserved.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#include <wrl.h>
#include <exception>
#include <d3d12.h>
#include <d3dx12.h>
#include <dxgi1_6.h>

using namespace Microsoft::WRL;

static int s_RenderWidth = 1080;
static int s_RenderHeight = 960;
static const int s_FrameCount = 2;

struct Pipeline
{
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDXGISwapChain3> swapchain;
    ComPtr<ID3D12Resource> renderTargets[s_FrameCount];
    ComPtr<ID3D12CommandAllocator> cmdAlloc;
    ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap;
    UINT rtvDescriptorSize;
};

inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
    {
        // Set a breakpoint on this line to catch DirectX API errors
        throw std::exception();
    }
}

// Main message handler for the app.
static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

static HRESULT FindD3D12HardwareAdapter(
    ComPtr<IDXGIFactory4> factory,
    ComPtr<IDXGIAdapter1> outAdapter)
{
    HRESULT r = E_NOINTERFACE;

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
        HRESULT r = D3D12CreateDevice(
            adapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            _uuidof(ID3D12Device),
            nullptr);

        if (SUCCEEDED(r))
        {
            outAdapter = adapter.Detach();
            r = S_OK;
            break;
        }
    }

    return r;
}

static void LoadPipeline(Pipeline* pPipeline, HWND hwnd)
{
    UINT dxgiFactoryFlag = 0;

#if defined(_DEBUG)
    // Enable debug layers.
    {
        ComPtr<ID3D12Debug> debugController;
        ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));

        // TODO: Should I hold to this, or let it be destroyed automatically?
        ComPtr<ID3D12Debug1> debugController1;
        ThrowIfFailed(debugController->QueryInterface(IID_PPV_ARGS(&debugController1)));

        debugController1->EnableDebugLayer();
        debugController1->SetEnableGPUBasedValidation(TRUE);

        dxgiFactoryFlag |= DXGI_CREATE_FACTORY_DEBUG;
    }
#endif

    ComPtr<IDXGIFactory4> dxgiFactory;
    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlag, IID_PPV_ARGS(&dxgiFactory)));

    // Find an Adapter that supports D3D12.
    ComPtr<IDXGIAdapter1> dxgiAdapter;
    ThrowIfFailed(FindD3D12HardwareAdapter(dxgiFactory, dxgiAdapter));

    // Create device.
    ThrowIfFailed(D3D12CreateDevice(
            dxgiAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&pPipeline->device)));

    // Create command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ThrowIfFailed(pPipeline->device->CreateCommandQueue(
            &queueDesc,
            IID_PPV_ARGS(&pPipeline->queue)));

    // Create swapchain.
    DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
    swapchainDesc.BufferCount = s_FrameCount;
    swapchainDesc.Width = s_RenderWidth;
    swapchainDesc.Height = s_RenderHeight;
    swapchainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapchainDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapchain;
    ThrowIfFailed(dxgiFactory->CreateSwapChainForHwnd(
        pPipeline->queue.Get(), // swapchain needs the queue so it force flush it
        hwnd,
        &swapchainDesc,
        nullptr,
        nullptr,
        &swapchain));

    // This example doesn't support fullscreen.
    ThrowIfFailed(dxgiFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));

    ThrowIfFailed(swapchain.As(&pPipeline->swapchain));

    // Create descriptor heaps.
    {
        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = s_FrameCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(pPipeline->device->CreateDescriptorHeap(
            &rtvHeapDesc,
            IID_PPV_ARGS(&pPipeline->rtvDescriptorHeap)));

        pPipeline->rtvDescriptorSize =
            pPipeline->device->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }

    // Create frame resource.
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvDescriptorHandle(
            pPipeline->rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

        // Create render target view for each frame.
        for (UINT i = 0; i < s_FrameCount; ++i)
        {
            ThrowIfFailed(pPipeline->swapchain->GetBuffer(
                i,
                IID_PPV_ARGS(&pPipeline->renderTargets[i])));

            pPipeline->device->CreateRenderTargetView(
                pPipeline->renderTargets[i].Get(),
                nullptr,
                rtvDescriptorHandle);

            // There are 2 diescriptors, advance to the next descriptor in memory.
            rtvDescriptorHandle.Offset(1, pPipeline->rtvDescriptorSize);
        }
    }

    ThrowIfFailed(pPipeline->device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&pPipeline->cmdAlloc)));
}

static void LoadAssets(Pipeline* pPipeline)
{
    // Create an empty root signature.
    {
        CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init()
    }
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR lpCmdLine, int nShowCmd)
{
    int result = 0;

    WNDCLASSEXA windowClass = {};
    windowClass.cbSize = sizeof(WNDCLASSEX);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = hInst;
    windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    windowClass.lpszClassName = "GPU Trasher";
    RegisterClassExA(&windowClass);

    RECT windowRect = { 0, 0, s_RenderWidth, s_RenderHeight };
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = CreateWindowA(
        windowClass.lpszClassName,
        "GPU Trasher",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr, // no parent window
        nullptr, // no menu
        hInst,
        nullptr);

    if (hwnd != NULL)
    {
        Pipeline pipeline;

        LoadPipeline(&pipeline, hwnd);

        ShowWindow(hwnd, nShowCmd);

        MSG msg = {};
        while (msg.message != WM_QUIT)
        {
            if (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
        }

        result = static_cast<int>(msg.wParam);
    }

    return result;
}

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        LPCREATESTRUCTA windowCreateParam = reinterpret_cast<LPCREATESTRUCTA>(lParam);
        SetWindowLongPtrA(
            hwnd,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(windowCreateParam->lpCreateParams));
    } break;

    case WM_DESTROY:
    {
        PostQuitMessage(0);
        return 0;
    } break;
    }

    // handle any messages the switch statement above didn't.
    return DefWindowProc(hwnd, message, wParam, lParam);
}
