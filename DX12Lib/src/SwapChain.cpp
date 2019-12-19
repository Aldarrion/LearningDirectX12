#include <DX12LibPCH.h>

#include <SwapChain.h>

#include <CommandList.h>
#include <CommandQueue.h>
#include <Device.h>
#include <ResourceStateTracker.h>

SwapChain::SwapChain(std::shared_ptr<Device> device, HWND hWnd)
    : m_Device(device)
    , m_hWnd(hWnd)
    , m_VSync(false)
    , m_IsTearingSupported(false)
    , m_FrameCounter(0)
{
    for (int i = 0; i < BufferCount; ++i)
    {
        m_BackBufferTextures[i].SetName(L"Backbuffer[" + std::to_wstring(i) + L"]");
    }

    m_dxgiSwapChain = CreateSwapChain();
    UpdateRenderTargetViews();

}

// Create the swapchian.
Microsoft::WRL::ComPtr<IDXGISwapChain4> SwapChain::CreateSwapChain()
{
    RECT clientRect;
    ::GetClientRect(m_hWnd, &clientRect);

    ComPtr<IDXGISwapChain4> dxgiSwapChain4;
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
    swapChainDesc.BufferCount = BufferCount;
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    // It is recommended to always allow tearing if tearing support is available.
    swapChainDesc.Flags = m_IsTearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    swapChainDesc.Flags |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    ID3D12CommandQueue* pCommandQueue = m_Device->GetCommandQueue()->GetD3D12CommandQueue().Get();

    ComPtr<IDXGISwapChain1> swapChain1;
    ThrowIfFailed(dxgiFactory->CreateSwapChainForHwnd(
        pCommandQueue,
        m_hWnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain1));

    // Disable the Alt+Enter fullscreen toggle feature. Switching to fullscreen
    // will be handled manually.
    ThrowIfFailed(dxgiFactory->MakeWindowAssociation(m_hWnd, DXGI_MWA_NO_ALT_ENTER));

    ThrowIfFailed(swapChain1.As(&dxgiSwapChain4));

    m_CurrentBackBufferIndex = dxgiSwapChain4->GetCurrentBackBufferIndex();
    dxgiSwapChain4->SetMaximumFrameLatency(BufferCount - 1);
    m_SwapChainEvent = dxgiSwapChain4->GetFrameLatencyWaitableObject();

    return dxgiSwapChain4;
}

void SwapChain::UpdateRenderTargetViews()
{
    for (int i = 0; i < BufferCount; ++i)
    {
        ComPtr<ID3D12Resource> backBuffer;
        ThrowIfFailed(m_dxgiSwapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));

        ResourceStateTracker::AddGlobalResourceState(backBuffer.Get(), D3D12_RESOURCE_STATE_COMMON);

        m_BackBufferTextures[i].SetD3D12Resource(backBuffer);
        m_BackBufferTextures[i].CreateViews();
    }
}

const RenderTarget& SwapChain::GetRenderTarget() const
{
    m_RenderTarget.AttachTexture(AttachmentPoint::Color0, m_BackBufferTextures[m_CurrentBackBufferIndex]);
    return m_RenderTarget;
}

UINT SwapChain::Present(const Texture& texture)
{
    auto commandQueue = m_Device.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
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
    m_FrameValues[m_CurrentBackBufferIndex] = m_Device.IncrementFrameCounter();

    m_CurrentBackBufferIndex = m_dxgiSwapChain->GetCurrentBackBufferIndex();

    commandQueue->WaitForFenceValue(m_FenceValues[m_CurrentBackBufferIndex]);

    m_Device.ReleaseStaleDescriptors(m_FrameValues[m_CurrentBackBufferIndex]);

    return m_CurrentBackBufferIndex;
}