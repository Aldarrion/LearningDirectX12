#include <DirectX12TemplatePCH.h>

#include <Application.h>
#include <Helpers.h>

#include <Window.h>

// Import namespaces.
using namespace Microsoft::WRL;

// Constants
constexpr auto WINDOW_CLASS_NAME = L"DX12WindowClass";

// Forward declarations
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM); // Defined in the Application class.

Window::Window(uint32_t width, uint32_t height, const std::wstring& name, bool fullscreen )
    : m_Width( width )
    , m_Height( height )
    , m_Fullscreen( fullscreen )
    , m_Name( name )
    , m_FenceValues{}
    , m_CurrentBackBufferIndex( 0 )
{
    // Check to see if the monitor supports variable refresh rates.
    m_AllowTearing = Application::Get().AllowTearing();

    // Create the descriptor heap for the render target views for the back buffers
    // of the swap chain.
    auto device = Application::Get().GetDevice();

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = FrameCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_RTVDescriptorHeap)));

    // Sizes of descriptors is vendor specific and must be queried at runtime.
    m_RTVDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    CreateWindow();
    CreateSwapChain();
}

Window::~Window()
{
    ::DestroyWindow(m_hWindow);
}

WNDCLASSEXW Window::GetWindowClassInfo(HINSTANCE hInst) const
{
    // Register a window class for creating our render windows with.
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
    windowClass.lpszClassName = WINDOW_CLASS_NAME;
    windowClass.hIconSm = ::LoadIcon(hInst, NULL); //  MAKEINTRESOURCE(APPLICATION_ICON));

    return windowClass;
}

void Window::CreateWindow()
{
    HINSTANCE hInstance = Application::Get().GetInstanceHandle();
    WNDCLASSEXW windowClass = GetWindowClassInfo(hInstance);

    // Store the result in a local static to ensure this function is called only once.
    static HRESULT hr = ::RegisterClassExW(&windowClass);

    int screenWidth = ::GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = ::GetSystemMetrics(SM_CYSCREEN);

    RECT windowRect = { 0, 0, static_cast<LONG>(m_Width), static_cast<LONG>(m_Height) };

    ::AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

    int windowWidth = windowRect.right - windowRect.left;
    int windowHeight = windowRect.bottom - windowRect.top;

    int windowX = std::max<int>(0, (screenWidth - windowWidth) / 2);
    int windowY = std::max<int>(0, (screenHeight - windowHeight) / 2);

    m_hWindow = ::CreateWindowExW(
        NULL,
        WINDOW_CLASS_NAME,
        m_Name.c_str(),
        WS_OVERLAPPEDWINDOW,
        windowX,
        windowY,
        windowWidth,
        windowHeight,
        NULL,
        NULL,
        hInstance,
        NULL
    );

    assert(m_hWindow && "Failed to create window");

    ::SetWindowTextW(m_hWindow, m_Name.c_str());
}

void Window::CreateSwapChain()
{
    ComPtr<IDXGIFactory4> dxgiFactory4;
    UINT createFactoryFlags = 0;
#if defined(_DEBUG)
    createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

    ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory4)));

    // Get the direct command queue from the application instance.
    // This is required to create the swap chain.
    auto commandQueue = Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
    // Make sure all GPU commands are finished before (re) creating the swap chain for this window.
    Application::Get().WaitForGPU();

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = m_Width;
    swapChainDesc.Height = m_Height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.Stereo = FALSE;
    swapChainDesc.SampleDesc = { 1, 0 };
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    // It is recommended to always allow tearing if tearing support is available.
    swapChainDesc.Flags = m_AllowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    ComPtr<IDXGISwapChain1> swapChain1;
    ThrowIfFailed(dxgiFactory4->CreateSwapChainForHwnd(
        commandQueue.Get(),
        m_hWindow,
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain1));

    // Disable the Alt+Enter fullscreen toggle feature. Switching to fullscreen
    // will be handled manually.
    dxgiFactory4->MakeWindowAssociation(m_hWindow, DXGI_MWA_NO_ALT_ENTER);

    ThrowIfFailed(swapChain1.As(&m_SwapChain));
    
    m_CurrentBackBufferIndex = m_SwapChain->GetCurrentBackBufferIndex();

    UpdateSwapChainRenderTargetViews();
}

void Window::UpdateSwapChainRenderTargetViews()
{
    auto device = Application::Get().GetDevice();

    // Get a handle to the first descriptor in the heap.
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    for (int i = 0; i < FrameCount; ++i)
    {
        ThrowIfFailed(m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&m_BackBuffers[i])));
        device->CreateRenderTargetView(m_BackBuffers[i].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(m_RTVDescriptorSize);
    }
}

void Window::Show()
{
    ::ShowWindow(m_hWindow, SW_SHOWDEFAULT);
}

void Window::Hide()
{
    ::ShowWindow(m_hWindow, SW_HIDE);
}

void Window::SetWindowTitle(const std::wstring& windowTitle)
{
    ::SetWindowTextW(m_hWindow, windowTitle.c_str());
}