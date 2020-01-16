#include <DX12LibPCH.h>

#include <SwapChain.h>

#include <CommandList.h>
#include <CommandQueue.h>
#include <Device.h>
#include <ResourceStateTracker.h>

SwapChain::SwapChain()
    : m_hWnd(0)
    , m_VSync(true)
    , m_IsTearingSupported(false)
    , m_FenceValues{0}
    , m_FrameValues{0}
    , m_FrameCounter(0)
    , m_dxgiSwapChain(nullptr)
    , m_SwapChainEvent(0)
    , m_BufferCount(0)
    , m_CurrentBackBufferIndex(0)
{}

SwapChain::SwapChain(HWND hWnd)
    : m_hWnd(hWnd)
    , m_VSync(true)
    , m_IsTearingSupported(false)
    , m_FrameCounter(0)
{
    uint32_t nodeCount = Device::Get().GetNodeCount();
    uint32_t buffersPerNode = nodeCount > 1 ? 1 : 2;
    m_BufferCount = buffersPerNode * nodeCount;

    m_FenceValues.resize(m_BufferCount);
    m_FrameValues.resize(m_BufferCount);
    m_BackBufferTextures.resize(m_BufferCount);

    for (uint32_t i = 0; i < m_BufferCount; ++i)
    {
        m_BackBufferTextures[i].SetName(L"Backbuffer[" + std::to_wstring(i) + L"]");
    }

    CreateSwapChain();
    UpdateRenderTargetViews();
}

// Create the swapchian.
void SwapChain::CreateSwapChain()
{
    RECT clientRect;
    ::GetClientRect(m_hWnd, &clientRect);

    ComPtr<IDXGIFactory7> dxgiFactory;
    UINT createFactoryFlags = 0;
#if defined(_DEBUG)
    createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

    ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory)));

    BOOL allowTearing = FALSE;
    dxgiFactory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));


    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = ( clientRect.right - clientRect.left );
    swapChainDesc.Height = ( clientRect.bottom - clientRect.top );
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.Stereo = FALSE;
    swapChainDesc.SampleDesc = { 1, 0 };
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = m_BufferCount;
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    // It is recommended to always allow tearing if tearing support is available.
    swapChainDesc.Flags = m_IsTearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    swapChainDesc.Flags |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    auto& device = Device::Get();
    auto d3d12Device = device.GetD3D12Device();
    auto commandQueue = device.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
    auto d3d12CommandQueue = commandQueue->GetD3D12CommandQueue();

    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(dxgiFactory->CreateSwapChainForHwnd(
        d3d12CommandQueue->GetChildObject(0),
        m_hWnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain));

    // Disable the Alt+Enter fullscreen toggle feature. Switching to fullscreen
    // will be handled manually.
    if ( m_IsTearingSupported )
    {
        ThrowIfFailed(dxgiFactory->MakeWindowAssociation(m_hWnd, DXGI_MWA_NO_ALT_ENTER));
    }

    ThrowIfFailed(DXGIXAffinityCreateLDASwapChain(swapChain.Get(), d3d12CommandQueue.Get(), d3d12Device.Get(), &m_dxgiSwapChain));

    m_CurrentBackBufferIndex = m_dxgiSwapChain->GetCurrentBackBufferIndex();
    m_dxgiSwapChain->SetMaximumFrameLatency(m_BufferCount - 1);
    m_SwapChainEvent = m_dxgiSwapChain->GetFrameLatencyWaitableObject();
}

void SwapChain::UpdateRenderTargetViews()
{
    for (uint32_t i = 0; i < m_BufferCount; ++i)
    {
        ComPtr<CD3DX12AffinityResource> backBuffer;
        ThrowIfFailed(m_dxgiSwapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));

        ResourceStateTracker::AddGlobalResourceState(backBuffer.Get(), D3D12_RESOURCE_STATE_COMMON);

        m_BackBufferTextures[i] = Texture(backBuffer, TextureUsage::RenderTarget, L"Backbuffer Texture");
        m_BackBufferTextures[i].CreateViews();
    }
}

void SwapChain::Resize(uint32_t width, uint32_t height)
{
    width = std::max(1u, width);
    height = std::max(1u, height);

    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    ThrowIfFailed(m_dxgiSwapChain->GetDesc(&swapChainDesc));
    
    // Update the client size.
    if (swapChainDesc.BufferDesc.Width != width || swapChainDesc.BufferDesc.Height != height)
    {
        Device::Get().Flush();

        // Release all references to back buffer textures.
        m_RenderTarget.AttachTexture(Color0, Texture());
        for (int i = 0; i < m_BufferCount; ++i)
        {
            ResourceStateTracker::RemoveGlobalResourceState(m_BackBufferTextures[i].GetD3D12Resource().Get());
            m_BackBufferTextures[i].Reset();
        }

        ThrowIfFailed(m_dxgiSwapChain->ResizeBuffers(m_BufferCount, width,
            height, swapChainDesc.BufferDesc.Format, swapChainDesc.Flags));

        m_CurrentBackBufferIndex = m_dxgiSwapChain->GetCurrentBackBufferIndex();

        UpdateRenderTargetViews();
    }
}


const RenderTarget& SwapChain::GetRenderTarget() const
{
    m_RenderTarget.AttachTexture(AttachmentPoint::Color0, m_BackBufferTextures[m_CurrentBackBufferIndex]);
    return m_RenderTarget;
}

bool SwapChain::IsVSync() const
{
    return m_VSync;
}

void SwapChain::SetVSync(bool vSync)
{
    m_VSync = vSync;
}

void SwapChain::ToggleVSync()
{
    m_VSync = !m_VSync;
}

UINT SwapChain::Present(const Texture& texture)
{
    auto commandQueue = Device::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
    auto commandList = commandQueue->GetCommandList();

    auto& backBuffer = m_BackBufferTextures[m_CurrentBackBufferIndex];

    if (texture.IsValid())
    {
        if (texture.GetD3D12ResourceDesc().SampleDesc.Count > 1)
        {
            commandList->ResolveSubresource(backBuffer, texture);
        }
        else
        {
            commandList->CopyResource(backBuffer, texture);
        }
    }

    commandList->TransitionBarrier(backBuffer, D3D12_RESOURCE_STATE_PRESENT);
    commandQueue->ExecuteCommandList(commandList);

    UINT syncInterval = m_VSync ? 1 : 0;
    UINT presentFlags = m_IsTearingSupported && !m_VSync ? DXGI_PRESENT_ALLOW_TEARING : 0;
    ThrowIfFailed(m_dxgiSwapChain->Present(syncInterval, presentFlags));

    m_FenceValues[m_CurrentBackBufferIndex] = commandQueue->Signal();
    m_FrameValues[m_CurrentBackBufferIndex] = Device::IncrementFrameCounter();

    m_CurrentBackBufferIndex = m_dxgiSwapChain->GetCurrentBackBufferIndex();

    commandQueue->WaitForFenceValue(m_FenceValues[m_CurrentBackBufferIndex]);

    Device::Get().ReleaseStaleDescriptors(m_FrameValues[m_CurrentBackBufferIndex]);

    return m_CurrentBackBufferIndex;
}