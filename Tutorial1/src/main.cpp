#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h> // For CommandLineToArgvW

// The min/max macros conflict with like-named member functions.
// Only use std::min and std::max defined in <algorithm>.
#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif

// In order to define a function called CreateWindow, the Windows macro needs to
// be undefined.
#if defined(CreateWindow)
#undef CreateWindow
#endif

// Windows Runtime Library. Needed for Microsoft::WRL::ComPtr<> template class.
#include <wrl.h>
using namespace Microsoft::WRL;

// DirectX 12 specific headers.
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>

// D3D12 extension library.
#include <d3dx12.h>

// STL Headers
#include <algorithm>
#include <cassert>
#include <chrono>
#include <sstream>

// Helper functions
#include <Helpers.h>

// The number of swap chain back buffers.
const uint8_t g_NumFrames = 2;
// Use WARP adapter
bool g_UseWarp = false;

uint32_t g_ClientWidth = 1280;
uint32_t g_ClientHeight = 720;

// Window registration and window creation.
void RegisterWindowClass( HINSTANCE hInst, const wchar_t* windowClassName );
HWND CreateWindow(const wchar_t* windowClassName, HINSTANCE hInst,
    const wchar_t* windowTitle, uint32_t width, uint32_t height);

// Window callback function.
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

// Utility functions.
void ParseCommandLineArguments();

// DirectX 12 Initialization functions

void EnableDebugLayer();
ComPtr<IDXGIAdapter4> GetAdapter( bool useWarp );

ComPtr<ID3D12Device2> CreateDevice( ComPtr<IDXGIAdapter4> adapter );

ComPtr<ID3D12CommandQueue> CreateCommandQueue( ComPtr<ID3D12Device2> device, 
    D3D12_COMMAND_LIST_TYPE type );

// Check for tearing support.
bool CheckTearingSupport();

ComPtr<IDXGISwapChain4> CreateSwapChain(HWND hWnd, 
    ComPtr<ID3D12CommandQueue> commandQueue, 
    uint32_t width, uint32_t height, uint32_t bufferCount );

ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(ComPtr<ID3D12Device2> device,
    D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors);

void UpdateRenderTargetViews(ComPtr<ID3D12Device2> device,
    ComPtr<IDXGISwapChain4> swapChain, ComPtr<ID3D12DescriptorHeap> descriptorHeap);

ComPtr<ID3D12GraphicsCommandList> CreateCommandList(ComPtr<ID3D12Device2> device,
    ComPtr<ID3D12CommandAllocator> commandAllocator, D3D12_COMMAND_LIST_TYPE type);

ComPtr<ID3D12CommandAllocator> CreateCommandAllocator(ComPtr<ID3D12Device2> device,
    D3D12_COMMAND_LIST_TYPE type);

// Synchronization functions
ComPtr<ID3D12Fence> CreateFence(ComPtr<ID3D12Device2> device);

HANDLE CreateEventHandle();

// Signal the command queue.
// Returns the fence value to wait for.
uint64_t Signal(ComPtr<ID3D12CommandQueue> commandQueue, ComPtr<ID3D12Fence> fence,
    uint64_t& fenceValue );

// Waits for a fence value to be reached before continuing.
void WaitForFenceValue(ComPtr<ID3D12Fence> fence, uint64_t fenceValue, HANDLE fenceEvent, 
    std::chrono::milliseconds duration = std::chrono::milliseconds::max());

// Make sure the command queue has completed all commands before continuing.
void Flush(ComPtr<ID3D12CommandQueue> commandQueue, ComPtr<ID3D12Fence> fence,
    uint64_t& fenceValue, HANDLE fenceEvent );

void Resize(ComPtr<IDXGISwapChain4> swapChain, uint32_t width, uint32_t height);

int CALLBACK wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow)
{
    // Window class name. Used for registering / creating the window.
    const wchar_t* windowClassName = L"DX12WindowClass";

    // By default, enable V-Sync.
    // Can be toggled with the V key.
    bool vSync = true;

    ParseCommandLineArguments();
    EnableDebugLayer();

    RegisterWindowClass( hInstance, windowClassName );
    auto hWnd = CreateWindow( windowClassName, hInstance, L"Learning DirectX 12", 
        g_ClientWidth, g_ClientHeight);

    ComPtr<IDXGIAdapter4> dxgiAdapter4 = GetAdapter(g_UseWarp);
    auto device = CreateDevice(dxgiAdapter4);
    auto commandQueue = CreateCommandQueue(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
    auto swapChain = CreateSwapChain(hWnd, commandQueue,
        g_ClientWidth, g_ClientHeight, g_NumFrames);

    ComPtr<ID3D12Resource> backBuffers[g_NumFrames];
    for (int i = 0; i < g_NumFrames; ++i)
    {
        ThrowIfFailed(swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffers[i])));
    }

    auto currentBackBufferIndex = swapChain->GetCurrentBackBufferIndex();

    auto rtvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, g_NumFrames);
    auto rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    UpdateRenderTargetViews(device, swapChain, rtvDescriptorHeap);

    ComPtr<ID3D12CommandAllocator> commandAllocators[g_NumFrames];
    for (int i = 0; i < g_NumFrames; ++i)
    {
        commandAllocators[i] = CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
    }
    auto commandList = CreateCommandList(device, 
        commandAllocators[currentBackBufferIndex], D3D12_COMMAND_LIST_TYPE_DIRECT);

    auto fence = CreateFence(device);
    auto fenceEvent = CreateEventHandle();

    uint64_t fenceValue = 1;
    uint64_t frameFenceValues[g_NumFrames] = {};
    
    ::ShowWindow(hWnd, SW_SHOW);

    auto tearingSupported = CheckTearingSupport();

    uint64_t frameCounter = 0;
    double elapsedSeconds = 0.0;
    std::chrono::high_resolution_clock clock;
    auto t0 = clock.now();

    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        if (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);

            switch (msg.message)
            {
            case WM_PAINT:
            {
                // Clear the render target.
                {
                    commandAllocators[currentBackBufferIndex]->Reset();
                    commandList->Reset(commandAllocators[currentBackBufferIndex].Get(), nullptr);

                    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                        backBuffers[currentBackBufferIndex].Get(),
                        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

                    commandList->ResourceBarrier(1, &barrier);

                    FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };
                    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
                        currentBackBufferIndex, rtvDescriptorSize);
                    commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
                }

                // Present
                {
                    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                        backBuffers[currentBackBufferIndex].Get(),
                        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
                    commandList->ResourceBarrier(1, &barrier);

                    commandList->Close();

                    ID3D12CommandList* const commandLists[] = {
                        commandList.Get()
                    };
                    commandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);

                    frameFenceValues[currentBackBufferIndex] = Signal(commandQueue, fence, fenceValue);

                    UINT syncInterval = vSync ? 1 : 0;
                    UINT presentFlags = tearingSupported && !vSync ? DXGI_PRESENT_ALLOW_TEARING : 0;
                    swapChain->Present(syncInterval, presentFlags);

                    currentBackBufferIndex = swapChain->GetCurrentBackBufferIndex();

                    WaitForFenceValue(fence, frameFenceValues[currentBackBufferIndex], fenceEvent);
                }

                frameCounter++;
                auto t1 = clock.now();
                auto deltaTime = t1 - t0;
                t0 = t1;

                elapsedSeconds += deltaTime.count() * 1e-9;
                if (elapsedSeconds > 1.0)
                {
                    char buffer[500];
                    auto fps = frameCounter / elapsedSeconds;
                    sprintf_s(buffer, 500, "Framerate: %f\n", fps);
                    OutputDebugString(buffer);

                    frameCounter = 0;
                    elapsedSeconds = 0.0;
                }
            }
            break;
            case WM_SYSKEYDOWN:
            case WM_KEYDOWN:
                switch (msg.wParam)
                {
                case 'V':
                    vSync = !vSync;
                    break;
                case VK_ESCAPE:
                    ::PostQuitMessage(0);
                    break;
                }

                break;
            case WM_SIZE:
            {
                RECT clientRect = {};
                ::GetClientRect(hWnd, &clientRect);

                int width = clientRect.right - clientRect.left;
                int height = clientRect.bottom - clientRect.top;

                // Flush the GPU queue to make sure the swap chain's back buffers
                // are not being referenced by an in-flight command list.
                Flush(commandQueue, fence, fenceValue, fenceEvent);

                for (int i = 0; i < g_NumFrames; ++i)
                {
                    // Any references to the back buffers must be released
                    // before the swap chain can be resized.
                    backBuffers[i].Reset();
                    frameFenceValues[i] = frameFenceValues[currentBackBufferIndex];
                }

                Resize(swapChain, width, height);
                UpdateRenderTargetViews(device, swapChain, rtvDescriptorHeap);

                for (int i = 0; i < g_NumFrames; ++i)
                {
                    ThrowIfFailed(swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffers[i])));
                }

                currentBackBufferIndex = swapChain->GetCurrentBackBufferIndex();

            }
            break;
            }

            ::DispatchMessage(&msg);
        }
    }

    Flush(commandQueue, fence, fenceValue, fenceEvent);

    ::CloseHandle(fenceEvent);
    
    int ret = 0;
    return ret;
}

void ParseCommandLineArguments()
{
    int argc;
    wchar_t** argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);

    for (size_t i = 0; i < argc; ++i)
    {
        if (::wcscmp(argv[i], L"-w") == 0 || ::wcscmp(argv[i], L"--width") == 0)
        {
            g_ClientWidth = ::wcstol(argv[++i], nullptr, 10);
        }
        if (::wcscmp(argv[i], L"-h") == 0 || ::wcscmp(argv[i], L"--height") == 0)
        {
            g_ClientHeight = ::wcstol(argv[++i], nullptr, 10);
        }
        if (::wcscmp(argv[i], L"-warp") == 0 || ::wcscmp(argv[i], L"--warp") == 0)
        {
            g_UseWarp = true;
        }
    }

    // Free memory allocated by CommandLineToArgvW
    LocalFree(argv);
}

void EnableDebugLayer()
{
#if defined(_DEBUG)
    // Always enable the debug layer before doing anything DX12 related
    // so all possible errors generated while creating DX12 objects
    // are caught by the debug layer.
    ComPtr<ID3D12Debug> debugInterface;
    ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface)));
    debugInterface->EnableDebugLayer();
#endif

}

void RegisterWindowClass( HINSTANCE hInst, const wchar_t* windowClassName )
{
    // Register a window class for creating our render window with.
    WNDCLASSEXW windowClass = {};

    windowClass.cbSize = sizeof(WNDCLASSEX);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &WndProc;
    windowClass.cbClsExtra = 0;
    windowClass.cbWndExtra = 0;
    windowClass.hInstance = hInst;
    windowClass.hIcon = ::LoadIcon(hInst, NULL); //  MAKEINTRESOURCE(APPLICATION_ICON));
    windowClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);
    windowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    windowClass.lpszMenuName = NULL;
    windowClass.lpszClassName = windowClassName;
    windowClass.hIconSm = ::LoadIcon(hInst, NULL); //  MAKEINTRESOURCE(APPLICATION_ICON));

    static HRESULT hr = ::RegisterClassExW(&windowClass);
    assert(SUCCEEDED(hr));
}

HWND CreateWindow(const wchar_t* windowClassName, HINSTANCE hInst,
    const wchar_t* windowTitle, uint32_t width, uint32_t height)
{
    int screenWidth = ::GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = ::GetSystemMetrics(SM_CYSCREEN);

    RECT windowRect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
    ::AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

    int windowWidth = windowRect.right - windowRect.left;
    int windowHeight = windowRect.bottom - windowRect.top;

    // Center the window within the screen. Clamp to 0, 0 for the top-left corner.
    int windowX = std::max<int>(0, (screenWidth - windowWidth) / 2);
    int windowY = std::max<int>(0, (screenHeight - windowHeight) / 2);

    HWND hWnd = ::CreateWindowExW(
        NULL,
        windowClassName,
        windowTitle,
        WS_OVERLAPPEDWINDOW,
        windowX,
        windowY,
        windowWidth,
        windowHeight,
        NULL,
        NULL,
        hInst,
        nullptr
    );

    assert(hWnd && "Failed to create window");

    return hWnd;
}

ComPtr<IDXGIAdapter4> GetAdapter(bool useWarp)
{
    ComPtr<IDXGIFactory4> dxgiFactory;
    UINT createFactoryFlags = 0;
#if defined(_DEBUG)
    createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

    ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory)));

    ComPtr<IDXGIAdapter1> dxgiAdapter1;
    ComPtr<IDXGIAdapter4> dxgiAdapter4;
    SIZE_T maxDedicatedVideoMemory = 0;

    if (useWarp)
    {
        ThrowIfFailed(dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&dxgiAdapter1)));
        ThrowIfFailed(dxgiAdapter1.As(&dxgiAdapter4));
    }
    else
    {
        for (UINT i = 0; dxgiFactory->EnumAdapters1(i, &dxgiAdapter1) != DXGI_ERROR_NOT_FOUND; ++i)
        {
            ComPtr<IDXGIAdapter4> tmpAdapter;
            ThrowIfFailed(dxgiAdapter1.As(&tmpAdapter));
            DXGI_ADAPTER_DESC1 dxgiAdapterDesc1;
            tmpAdapter->GetDesc1(&dxgiAdapterDesc1);

            // Check to see if the adapter can create a D3D12 device without actually 
            // creating it. The adapter with the largest dedicated video memory
            // is favored.
            if ((dxgiAdapterDesc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
                SUCCEEDED(D3D12CreateDevice(dxgiAdapter4.Get(), 
                    D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)) && 
                dxgiAdapterDesc1.DedicatedVideoMemory > maxDedicatedVideoMemory )
            {
                maxDedicatedVideoMemory = dxgiAdapterDesc1.DedicatedVideoMemory;
                dxgiAdapter4 = tmpAdapter;
            }
        }
    }

    return dxgiAdapter4;
}

ComPtr<ID3D12Device2> CreateDevice(ComPtr<IDXGIAdapter4> adapter)
{
    ComPtr<ID3D12Device2> d3d12Device2;
    ThrowIfFailed(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d12Device2)));

    // Enable debug messages in debug mode.
#if defined(_DEBUG)
    ComPtr<ID3D12InfoQueue> pInfoQueue;
    if (SUCCEEDED(d3d12Device2.As(&pInfoQueue)))
    {
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);

        // Suppress whole categories of messages
        //D3D12_MESSAGE_CATEGORY Categories[] = {};

        // Suppress messages based on their severity level
        D3D12_MESSAGE_SEVERITY Severities[] =
        {
            D3D12_MESSAGE_SEVERITY_INFO
        };

        // Suppress individual messages by their ID
        D3D12_MESSAGE_ID DenyIds[] = {
            D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,   // I'm really not sure how to avoid this message.
            D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,                         // This warning occurs when using capture frame while graphics debugging.
            D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,                       // This warning occurs when using capture frame while graphics debugging.
        };

        D3D12_INFO_QUEUE_FILTER NewFilter = {};
        //NewFilter.DenyList.NumCategories = _countof(Categories);
        //NewFilter.DenyList.pCategoryList = Categories;
        NewFilter.DenyList.NumSeverities = _countof(Severities);
        NewFilter.DenyList.pSeverityList = Severities;
        NewFilter.DenyList.NumIDs = _countof(DenyIds);
        NewFilter.DenyList.pIDList = DenyIds;

        pInfoQueue->PushStorageFilter(&NewFilter);
    }
#endif

    return d3d12Device2;
}

ComPtr<ID3D12CommandQueue> CreateCommandQueue(ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type )
{
    ComPtr<ID3D12CommandQueue> d3d12CommandQueue;

    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type =     type;
    desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    desc.Flags =    D3D12_COMMAND_QUEUE_FLAG_NONE;
    desc.NodeMask = 0;

    ThrowIfFailed(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&d3d12CommandQueue)));

    return d3d12CommandQueue;
}

bool CheckTearingSupport()
{
    BOOL allowTearing = FALSE;

    // Rather than create the DXGI 1.5 factory interface directly, we create the
    // DXGI 1.4 interface and query for the 1.5 interface. This is to enable the 
    // graphics debugging tools which will not support the 1.5 factory interface 
    // until a future update.
    ComPtr<IDXGIFactory4> factory4;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory4))))
    {
        ComPtr<IDXGIFactory5> factory5;
        if (SUCCEEDED(factory4.As(&factory5)))
        {
            if (FAILED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing))))
            {
                allowTearing = FALSE;
            }
        }
    }

    return allowTearing == TRUE;
}

ComPtr<IDXGISwapChain4> CreateSwapChain(HWND hWnd, 
    ComPtr<ID3D12CommandQueue> commandQueue, 
    uint32_t width, uint32_t height, uint32_t bufferCount )
{
    ComPtr<IDXGISwapChain4> dxgiSwapChain4;
    ComPtr<IDXGIFactory4> dxgiFactory4;
    UINT createFactoryFlags = 0;
#if defined(_DEBUG)
    createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

    ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory4)));

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = width;
    swapChainDesc.Height = height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.Stereo = FALSE;
    swapChainDesc.SampleDesc = { 1, 0 };
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = bufferCount;
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    // It is recommended to always allow tearing if tearing support is available.
    swapChainDesc.Flags = CheckTearingSupport() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    ComPtr<IDXGISwapChain1> swapChain1;
    ThrowIfFailed(dxgiFactory4->CreateSwapChainForHwnd(
        commandQueue.Get(),
        hWnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain1));

    // Disable the Alt+Enter fullscreen toggle feature. Switching to fullscreen
    // will be handled manually.
    dxgiFactory4->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);

    ThrowIfFailed(swapChain1.As(&dxgiSwapChain4));

    //m_CurrentBackBufferIndex = m_SwapChain->GetCurrentBackBufferIndex();

    //UpdateSwapChainRenderTargetViews();

    return dxgiSwapChain4;
}

ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(ComPtr<ID3D12Device2> device,
    D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors)
{
    ComPtr<ID3D12DescriptorHeap> descriptorHeap;

    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors = numDescriptors;
    desc.Type = type;

    ThrowIfFailed(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptorHeap)));

    return descriptorHeap;
}

void UpdateRenderTargetViews(ComPtr<ID3D12Device2> device,
    ComPtr<IDXGISwapChain4> swapChain, ComPtr<ID3D12DescriptorHeap> descriptorHeap)
{
    auto rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(descriptorHeap->GetCPUDescriptorHandleForHeapStart());

    for (int i = 0; i < g_NumFrames; ++i)
    {
        ComPtr<ID3D12Resource> backBuffer;
        ThrowIfFailed(swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));

        device->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvHandle);

        rtvHandle.Offset(rtvDescriptorSize);
    }
}

ComPtr<ID3D12CommandAllocator> CreateCommandAllocator(ComPtr<ID3D12Device2> device,
    D3D12_COMMAND_LIST_TYPE type)
{
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    ThrowIfFailed(device->CreateCommandAllocator(type, IID_PPV_ARGS(&commandAllocator)));

    return commandAllocator;
}

ComPtr<ID3D12GraphicsCommandList> CreateCommandList(ComPtr<ID3D12Device2> device,
    ComPtr<ID3D12CommandAllocator> commandAllocator, D3D12_COMMAND_LIST_TYPE type)
{
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ThrowIfFailed(device->CreateCommandList(0, type, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));
    
    commandList->Close();

    return commandList;
}


ComPtr<ID3D12Fence> CreateFence(ComPtr<ID3D12Device2> device)
{
    ComPtr<ID3D12Fence> fence;

    // Create fence and event objects for GPU/CPU synchronization.
    ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));

    return fence;
}

HANDLE CreateEventHandle()
{
    HANDLE fenceEvent;
    
    fenceEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
    assert(fenceEvent && "Failed to create fence event.");

    return fenceEvent;
}

uint64_t Signal(ComPtr<ID3D12CommandQueue> commandQueue, ComPtr<ID3D12Fence> fence,
    uint64_t& fenceValue)
{
    uint64_t fenceValueForSignal = fenceValue++;
    ThrowIfFailed(commandQueue->Signal(fence.Get(), fenceValueForSignal));

    return fenceValueForSignal;
}

void WaitForFenceValue(ComPtr<ID3D12Fence> fence, uint64_t fenceValue, HANDLE fenceEvent,
    std::chrono::milliseconds duration)
{
    if (fence->GetCompletedValue() < fenceValue)
    {
        ThrowIfFailed(fence->SetEventOnCompletion(fenceValue, fenceEvent));
        ::WaitForSingleObject(fenceEvent, static_cast<DWORD>(duration.count()));
    }
}

void Flush(ComPtr<ID3D12CommandQueue> commandQueue, ComPtr<ID3D12Fence> fence,
    uint64_t& fenceValue, HANDLE fenceEvent )
{
    uint64_t fenceValueForSignal = Signal(commandQueue, fence, fenceValue);
    WaitForFenceValue(fence, fenceValueForSignal, fenceEvent);
}

void Update()
{

}

void Render()
{

}

void Resize(ComPtr<IDXGISwapChain4> swapChain, uint32_t width, uint32_t height)
{
    if (g_ClientWidth != width || g_ClientHeight != height)
    {
        g_ClientWidth = width;
        g_ClientHeight = height;

        DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
        ThrowIfFailed(swapChain->GetDesc(&swapChainDesc));
        ThrowIfFailed(swapChain->ResizeBuffers(g_NumFrames, width, height,
            swapChainDesc.BufferDesc.Format, swapChainDesc.Flags));
    }
}


LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_SIZE:
    case WM_DESTROY:
    case WM_PAINT:
    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
        break;
    default:
        return ::DefWindowProcW(hwnd, message, wParam, lParam);
    }

    return 0;
}