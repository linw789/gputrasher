// Copyright (c) 2022 Advanced Micro Devices, Inc. All rights reserved.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#include <wrl.h>
#include <exception>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include "d3dx12.h"

using namespace Microsoft::WRL;
using namespace DirectX;

static int s_RenderWidth = 1080;
static int s_RenderHeight = 960;
static const int s_FrameCount = 2;

struct Vertex
{
    XMFLOAT3 position;
    XMFLOAT4 color;
};

struct Pipeline
{
    // pipeline objects
    CD3DX12_VIEWPORT viewport;
    CD3DX12_RECT scissorRect;
    ComPtr<IDXGISwapChain3> swapchain;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12Resource> renderTargets[s_FrameCount];
    ComPtr<ID3D12CommandAllocator> cmdAlloc;
    ComPtr<ID3D12CommandQueue> cmdQueue;
    ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap;
    UINT rtvDescriptorSize;
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3D12PipelineState> pipelineState;
    ComPtr<ID3D12GraphicsCommandList> cmdList;

    // app resource
    ComPtr<ID3D12Resource> vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView;

    // synchronization
    UINT frameIndex;
    HANDLE fenceEvent;
    ComPtr<ID3D12Fence> fence;
    UINT64 fenceValue;
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

static void WaitForPreviousFrame(Pipeline* pPipeline)
{
    // WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
    // This is code implemented as such for simplicity. The D3D12HelloFrameBuffering
    // sample illustrates how to use fences for efficient resource usage and to
    // maximize GPU utilization.

    // Increment the fence value from CPU side.
    pPipeline->fenceValue += 1;

    // Add a command to set fence value from GPU side to CPU `fenceValue`.
    ThrowIfFailed(pPipeline->cmdQueue->Signal(
        pPipeline->fence.Get(),
        pPipeline->fenceValue));

    // Wait until the previous frame is finished.
    if (pPipeline->fence->GetCompletedValue() < pPipeline->fenceValue)
    {
        ThrowIfFailed(pPipeline->fence->SetEventOnCompletion(
            pPipeline->fenceValue,
            pPipeline->fenceEvent));

        WaitForSingleObject(pPipeline->fenceEvent, INFINITE);
    }

    pPipeline->frameIndex = pPipeline->swapchain->GetCurrentBackBufferIndex();
}

static void LoadPipeline(Pipeline* pPipeline, HWND hwnd)
{
    pPipeline->viewport = CD3DX12_VIEWPORT(
        0.0f,
        0.0f,
        (float)s_RenderWidth,
        (float)s_RenderHeight);

    pPipeline->scissorRect = CD3DX12_RECT(
        0.0f,
        0.0f,
        (float)s_RenderWidth,
        (float)s_RenderHeight);

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
            IID_PPV_ARGS(&pPipeline->cmdQueue)));

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
        // swapchain needs the command queue so it force flush it
        pPipeline->cmdQueue.Get(),
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
        rootSignatureDesc.Init(
            0, // number of parameters
            nullptr, // parameter array
            0, // number of static samplers
            nullptr, // static sampler array
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        ThrowIfFailed(D3D12SerializeRootSignature(
            &rootSignatureDesc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &signature,
            &error));

        ThrowIfFailed(pPipeline->device->CreateRootSignature(
            0,
            signature->GetBufferPointer(),
            signature->GetBufferSize(),
            IID_PPV_ARGS(&pPipeline->rootSignature)));
    }

    // Create pipeline state, which includes compiling and loading shaders.
    {
        ComPtr<ID3DBlob> vertexShader;
        ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
        // Enable better shader debugging with the graphics debugging tools.
        UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        UINT compileFlag = 0;
#endif

        {
            ComPtr<ID3DBlob> compileError;
            HRESULT compileResult = D3DCompileFromFile(
                L"C:/projects/gputrasher/src/hello-triangle.hlsl",
                nullptr,
                nullptr,
                "VSMain",
                "vs_5_0",
                compileFlags,
                0,
                &vertexShader,
                &compileError);

            if (FAILED(compileResult))
            {
                OutputDebugStringA((char*)compileError->GetBufferPointer());
                throw std::exception();
            }

        }

        {
            ComPtr<ID3DBlob> compileError;
            HRESULT compileResult = D3DCompileFromFile(
                L"C:/projects/gputrasher/src/hello-triangle.hlsl",
                nullptr,
                nullptr,
                "PSMain",
                "ps_5_0",
                compileFlags,
                0,
                &pixelShader,
                &compileError);

            if (FAILED(compileResult))
            {
                OutputDebugStringA((char*)compileError->GetBufferPointer());
                throw std::exception();
            }
        }

        // vertex input layout
        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };

        // graphics pipeline state object (PSO).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
        psoDesc.pRootSignature = pPipeline->rootSignature.Get();
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthEnable = FALSE;
        psoDesc.DepthStencilState.StencilEnable = FALSE;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.SampleDesc.Count = 1;
        ThrowIfFailed(pPipeline->device->CreateGraphicsPipelineState(
            &psoDesc,
            IID_PPV_ARGS(&pPipeline->pipelineState)));
    }

    ThrowIfFailed(pPipeline->device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        pPipeline->cmdAlloc.Get(),
        pPipeline->pipelineState.Get(),
        IID_PPV_ARGS(&pPipeline->cmdList)));

    // Command lists are created in the recording state, but there is nothing
    // to record yet. The main loop expects it to be closed, so close it now.
    ThrowIfFailed(pPipeline->cmdList->Close());

    // Create the vertex buffer.
    {
        const float aspectRatio = (float)s_RenderWidth / (float)s_RenderHeight;

        Vertex vertices[] =
        {
            { { 0.0f, 0.25f * aspectRatio, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
            { { 0.25f, -0.25f * aspectRatio, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
            { { -0.25f, -0.25f * aspectRatio, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } }
        };

        const UINT vertexBufferSize = sizeof(vertices);

        // Note: using upload heaps to transfer static data like vert buffers is not
        // recommended. Every time the GPU needs it, the upload heap will be marshalled
        // over. Please read up on Default Heap usage. An upload heap is used here for
        // code simplicity and because there are very few verts to actually transfer.
        ThrowIfFailed(pPipeline->device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&pPipeline->vertexBuffer)));

        // Copy the triangle data to the vertex buffer.
        UINT8* pVertexData;
        CD3DX12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU.
        ThrowIfFailed(pPipeline->vertexBuffer->Map(
            0,
            &readRange,
            reinterpret_cast<void**>(&pVertexData)));
        memcpy(pVertexData, vertices, vertexBufferSize);
        pPipeline->vertexBuffer->Unmap(0, nullptr);

        // Initialize the vertex buffer view.
        pPipeline->vertexBufferView.BufferLocation =
            pPipeline->vertexBuffer->GetGPUVirtualAddress();
        pPipeline->vertexBufferView.StrideInBytes = sizeof(Vertex);
        pPipeline->vertexBufferView.SizeInBytes = vertexBufferSize;
    }

    // Create synchronization objects and wait until assets have been uploaded to the GPU.
    {
        pPipeline->fenceValue = 0;

        ThrowIfFailed(pPipeline->device->CreateFence(
            pPipeline->fenceValue, // initial fence value
            D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(&pPipeline->fence)));

        // Create an event handle to use for frame synchronization.
        pPipeline->fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (pPipeline->fenceEvent == nullptr)
        {
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        }

        // Wait for the command list to execute; we are reusing the same command
        // list in our main loop but for now, we just want to wait for setup to
        // complete before continuing.
        WaitForPreviousFrame(pPipeline);
    }
}

static void PopulateCommandList(Pipeline* pPipeline)
{
    // Command list allocators can only be reset when the associated
    // command lists have finished execution on the GPU; apps should use
    // fences to determine GPU execution progress.
    ThrowIfFailed(pPipeline->cmdAlloc->Reset());

    // However, when ExecuteCommandList() is called on a particular command
    // list, that command list can then be reset at any time and must be before
    // re-recording.
    ThrowIfFailed(pPipeline->cmdList->Reset(
        pPipeline->cmdAlloc.Get(),
        pPipeline->pipelineState.Get()));

    // Set necessary state.
    pPipeline->cmdList->SetGraphicsRootSignature(pPipeline->rootSignature.Get());
    pPipeline->cmdList->RSSetViewports(1, &pPipeline->viewport);
    pPipeline->cmdList->RSSetScissorRects(1, &pPipeline->scissorRect);

    // Indicate that the back buffer will be used as a render target.
    {
        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            pPipeline->renderTargets[pPipeline->frameIndex].Get(),
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET);

        pPipeline->cmdList->ResourceBarrier(1, &barrier);
    }

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
        pPipeline->rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
        pPipeline->frameIndex,
        pPipeline->rtvDescriptorSize);

    pPipeline->cmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    // Record commands.
    const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
    pPipeline->cmdList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    pPipeline->cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    pPipeline->cmdList->IASetVertexBuffers(0, 1, &pPipeline->vertexBufferView);
    pPipeline->cmdList->DrawInstanced(3, 1, 0, 0);

    // Indicate that the back buffer will now be used to present.
    {
        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            pPipeline->renderTargets[pPipeline->frameIndex].Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT);

        pPipeline->cmdList->ResourceBarrier(1, &barrier);
    }

    ThrowIfFailed(pPipeline->cmdList->Close());
}

static void Render(Pipeline* pPipeline)
{
    // Record all the commands we need to render the scene into the command list.
    PopulateCommandList(pPipeline);

    // Execute the command list.
    ID3D12CommandList* ppCommandLists[] = { pPipeline->cmdList.Get() };
    pPipeline->cmdQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Present the frame.
    ThrowIfFailed(pPipeline->swapchain->Present(1, 0));

    WaitForPreviousFrame(pPipeline);
}

static void Destroy(Pipeline* pPipeline)
{
    WaitForPreviousFrame(pPipeline);
    CloseHandle(pPipeline->fenceEvent);
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

    Pipeline pipeline;

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
        &pipeline);

    if (hwnd != NULL)
    {

        LoadPipeline(&pipeline, hwnd);
        LoadAssets(&pipeline);

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

        Destroy(&pipeline);

        result = static_cast<int>(msg.wParam);
    }

    return result;
}

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    Pipeline* pPipeline = reinterpret_cast<Pipeline*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

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

    case WM_PAINT:
    {
        Render(pPipeline);
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
