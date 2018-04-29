#include <DX12LibPCH.h>

#include <CommandList.h>

#include <Application.h>
#include <ByteAddressBuffer.h>
#include <ConstantBuffer.h>
#include <CommandQueue.h>
#include <DynamicDescriptorHeap.h>
#include <IndexBuffer.h>
#include <Resource.h>
#include <ResourceStateTracker.h>
#include <RootSignature.h>
#include <StructuredBuffer.h>
#include <Texture.h>
#include <UploadBuffer.h>
#include <VertexBuffer.h>

#include <DirectXTex/DDSTextureLoader12.h>
#include <DirectXTex/WICTextureLoader12.h>

#include <d3dx12.h>

#include <filesystem>
#include <memory> // For std::unique_ptr
#include <vector> // For std::vector

namespace fs = std::experimental::filesystem;
using namespace DirectX;

std::map<std::wstring, ID3D12Resource* > CommandList::ms_TextureCache;
std::mutex CommandList::ms_TextureCacheMutex;

CommandList::CommandList(D3D12_COMMAND_LIST_TYPE type)
    : m_d3d12CommandListType(type)
{
    auto device = Application::Get().GetDevice();

    ThrowIfFailed(device->CreateCommandAllocator(m_d3d12CommandListType, IID_PPV_ARGS(&m_d3d12CommandAllocator)));

    ThrowIfFailed(device->CreateCommandList(0, m_d3d12CommandListType, m_d3d12CommandAllocator.Get(),
        nullptr, IID_PPV_ARGS(&m_d3d12CommandList)));

    m_UploadBuffer = std::make_unique<UploadBuffer>();

    m_ResourceStateTracker = std::make_unique<ResourceStateTracker>();

    for (int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
    {
        m_DynamicDescriptorHeap[i] = std::make_unique<DynamicDescriptorHeap>(static_cast<D3D12_DESCRIPTOR_HEAP_TYPE>(i));
        m_DescriptorHeaps[i] = nullptr;
    }
}

CommandList::~CommandList()
{}

void CommandList::TransitionBarrier(const Resource& resource, D3D12_RESOURCE_STATES stateAfter, UINT subResource, bool flushBarriers)
{
    auto d3d12Resource = resource.GetD3D12Resource();
    if ( d3d12Resource )
    {
        // The "before" state is not important. It will be resolved by the resource state tracker.
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(d3d12Resource.Get(), D3D12_RESOURCE_STATE_COMMON, stateAfter, subResource);
        m_ResourceStateTracker->ResourceBarrier(barrier);
    }

    if (flushBarriers)
    {
        FlushResourceBarriers();
    }
}

void CommandList::UAVBarrier(const Resource& resource, bool flushBarriers)
{
    auto d3d12Resource = resource.GetD3D12Resource();
    if (d3d12Resource)
    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(d3d12Resource.Get());
        m_ResourceStateTracker->ResourceBarrier(barrier);
    }

    if (flushBarriers)
    {
        FlushResourceBarriers();
    }
}

void CommandList::FlushResourceBarriers()
{
    m_ResourceStateTracker->FlushResourceBarriers(*this);
}

void CommandList::CopyBuffer(Buffer& buffer, size_t numElements, size_t elementSize, const void* bufferData, D3D12_RESOURCE_FLAGS flags)
{
    auto device = Application::Get().GetDevice();

    size_t bufferSize = numElements * elementSize;

    ComPtr<ID3D12Resource> d3d12Resource;
    if (bufferSize == 0)
    {
        // This will result in a NULL resource (which may be desired to define a default resource.
    }
    else
    {
        ThrowIfFailed(device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(bufferSize, flags),
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&d3d12Resource)));

        // Add the resource to the global resource state tracker.
        ResourceStateTracker::AddGlobalResourceState(d3d12Resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST );

        if (bufferData != nullptr)
        {
            // Create an upload resource to use as an intermediate buffer to copy the buffer resource 
            ComPtr<ID3D12Resource> uploadResource;
            ThrowIfFailed(device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
                D3D12_HEAP_FLAG_NONE,
                &CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&uploadResource)));

            D3D12_SUBRESOURCE_DATA subresourceData = {};
            subresourceData.pData = bufferData;
            subresourceData.RowPitch = bufferSize;
            subresourceData.SlicePitch = subresourceData.RowPitch;

            UpdateSubresources(m_d3d12CommandList.Get(), d3d12Resource.Get(), uploadResource.Get(), 0, 0, 1, &subresourceData);

            // Add references to resources so they stay in scope until the command list is reset.
            m_TrackedObjects.push_back(uploadResource);
        }
        m_TrackedObjects.push_back(d3d12Resource);
    }

    buffer.SetD3D12Resource(d3d12Resource);
    buffer.CreateViews(numElements, elementSize);
}

void CommandList::CopyVertexBuffer(VertexBuffer& vertexBuffer, size_t numVertices, size_t vertexStride, const void* vertexBufferData)
{
    CopyBuffer(vertexBuffer, numVertices, vertexStride, vertexBufferData);
}

void CommandList::CopyIndexBuffer(IndexBuffer& indexBuffer, size_t numIndicies, DXGI_FORMAT indexFormat, const void* indexBufferData)
{
    size_t indexSizeInBytes = indexFormat == DXGI_FORMAT_R16_UINT ? 2 : 4;
    CopyBuffer(indexBuffer, numIndicies, indexSizeInBytes, indexBufferData);
}

void CommandList::CopyByteAddressBuffer( ByteAddressBuffer& byteAddressBuffer, size_t bufferSize, const void* bufferData )
{
    CopyBuffer( byteAddressBuffer, 1, bufferSize, bufferData, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS );
}

void CommandList::CopyStructuredBuffer( StructuredBuffer& structuredBuffer, size_t numElements, size_t elementSize, const void* bufferData )
{
    CopyBuffer( structuredBuffer, numElements, elementSize, bufferData, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS );
}

void CommandList::SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY primitiveTopology)
{
    m_d3d12CommandList->IASetPrimitiveTopology(primitiveTopology);
}

void CommandList::LoadTextureFromFile(Texture& texture, const std::wstring& fileName)
{
    auto device = Application::Get().GetDevice();

    fs::path filePath(fileName);
    if (!fs::exists(filePath))
    {
        throw std::invalid_argument("Invalid filename specified.");
    }
    auto iter = ms_TextureCache.find(fileName);
    if (iter != ms_TextureCache.end())
    {
        texture.SetD3D12Resource(iter->second);
    }
    else
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> textureResource;
        std::unique_ptr<uint8_t[]> textureData;
        std::vector<D3D12_SUBRESOURCE_DATA> subresourceData;

        if (filePath.extension() == ".dds")
        {
            // Use DDS texture loader.
            ThrowIfFailed(LoadDDSTextureFromFileEx(device.Get(),
                fileName.c_str(), 0, D3D12_RESOURCE_FLAG_NONE, 
                DDS_LOADER_MIP_RESERVE, &textureResource, textureData, 
                subresourceData
            ));
        }
        else
        {
            D3D12_SUBRESOURCE_DATA resourceData;
            // Use WIC texture loader.
            ThrowIfFailed(LoadWICTextureFromFileEx(device.Get(),
                fileName.c_str(), 0, D3D12_RESOURCE_FLAG_NONE, 
                WIC_LOADER_MIP_RESERVE, &textureResource, textureData, 
                resourceData
            ));

            subresourceData.push_back(resourceData);
        }

        // Update the global state tracker.
        ResourceStateTracker::AddGlobalResourceState(textureResource.Get(), D3D12_RESOURCE_STATE_COPY_DEST);

        texture.SetD3D12Resource(textureResource);
        texture.CreateViews();

        CopyTextureSubresource(texture, 0, static_cast<uint32_t>(subresourceData.size()), subresourceData.data());

        if ( subresourceData.size() < textureResource->GetDesc().MipLevels )
        {
            GenerateMips( texture );
        }

        // Add the texture resource to the texture cache.
        std::lock_guard<std::mutex> lock(ms_TextureCacheMutex);
        ms_TextureCache[fileName] = textureResource.Get();
    }
}

void CommandList::GenerateMips( Texture& texture )
{
    if ( m_d3d12CommandListType == D3D12_COMMAND_LIST_TYPE_COPY && !m_GenerateMipsCommandList )
    {
        m_GenerateMipsCommandList = Application::Get().GetCommandQueue( D3D12_COMMAND_LIST_TYPE_COMPUTE )->GetCommandList();
    }

    // TODO: Generate mips.

}

void CommandList::CopyTextureSubresource(Texture& texture, uint32_t firstSubresource, uint32_t numSubresources, D3D12_SUBRESOURCE_DATA* subresourceData)
{
    auto device = Application::Get().GetDevice();
    auto destinationResource = texture.GetD3D12Resource();
    if ( destinationResource )
    {
        // Resource must be in the copy-destination state.
        TransitionBarrier(texture, D3D12_RESOURCE_STATE_COPY_DEST, true);

        UINT64 requiredSize = GetRequiredIntermediateSize(destinationResource.Get(), firstSubresource, numSubresources);

        // Create a temporary (intermediate) resource for uploading the subresources
        ComPtr<ID3D12Resource> intermediateResource;
        ThrowIfFailed(device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(requiredSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&intermediateResource)
        ));

        UpdateSubresources(m_d3d12CommandList.Get(), destinationResource.Get(), intermediateResource.Get(), 0, firstSubresource, numSubresources, subresourceData );

        m_TrackedObjects.push_back(intermediateResource);
        m_TrackedObjects.push_back(destinationResource);
    }
}

void CommandList::SetGraphicsDynamicConstantBuffer(uint32_t rootParameterIndex, size_t sizeInBytes, const void* bufferData)
{
    // Constant buffers must be 256-byte aligned.
    auto heapAllococation = m_UploadBuffer->Allocate(sizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    memcpy(heapAllococation.CPU, bufferData, sizeInBytes);
    m_d3d12CommandList->SetGraphicsRootConstantBufferView(rootParameterIndex, heapAllococation.GPU);
}

void CommandList::SetGraphics32BitConstants(uint32_t rootParameterIndex, uint32_t numConstants, const void* constants)
{
    m_d3d12CommandList->SetGraphicsRoot32BitConstants(rootParameterIndex, numConstants, constants, 0);
}

void CommandList::SetVertexBuffer(uint32_t slot, const VertexBuffer& vertexBuffer)
{
    m_ResourceStateTracker->TransitionResource(vertexBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

    auto vertexBufferView = vertexBuffer.GetVertexBufferView();
    
    m_d3d12CommandList->IASetVertexBuffers(slot, 1, &vertexBufferView);

    m_TrackedObjects.push_back(vertexBuffer.GetD3D12Resource());
}

void CommandList::SetDynamicVertexBuffer(uint32_t slot, size_t numVertices, size_t vertexSize, const void* vertexBufferData)
{
    size_t bufferSize = numVertices * vertexSize;

    auto heapAllocation = m_UploadBuffer->Allocate(bufferSize, vertexSize);
    memcpy(heapAllocation.CPU, vertexBufferData, bufferSize);

    D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
    vertexBufferView.BufferLocation = heapAllocation.GPU;
    vertexBufferView.SizeInBytes = static_cast<UINT>(bufferSize);
    vertexBufferView.StrideInBytes = static_cast<UINT>(vertexSize);

    m_d3d12CommandList->IASetVertexBuffers(slot, 1, &vertexBufferView);
}

void CommandList::SetIndexBuffer(const IndexBuffer& indexBuffer)
{
    m_ResourceStateTracker->TransitionResource(indexBuffer, D3D12_RESOURCE_STATE_INDEX_BUFFER);

    auto indexBufferView = indexBuffer.GetIndexBufferView();

    m_d3d12CommandList->IASetIndexBuffer(&indexBufferView);

    m_TrackedObjects.push_back(indexBuffer.GetD3D12Resource());
}

void CommandList::SetDynamicIndexBuffer(size_t numIndicies, DXGI_FORMAT indexFormat, const void* indexBufferData)
{
    size_t indexSizeInBytes = indexFormat == DXGI_FORMAT_R16_UINT ? 2 : 4;
    size_t bufferSize = numIndicies * indexSizeInBytes;

    auto heapAllocation = m_UploadBuffer->Allocate(bufferSize, indexSizeInBytes);
    memcpy(heapAllocation.CPU, indexBufferData, bufferSize);

    D3D12_INDEX_BUFFER_VIEW indexBufferView = {};
    indexBufferView.BufferLocation = heapAllocation.GPU;
    indexBufferView.SizeInBytes = static_cast<UINT>(bufferSize);
    indexBufferView.Format = indexFormat;

    m_d3d12CommandList->IASetIndexBuffer(&indexBufferView);
}

void CommandList::SetGraphicsDynamicStructuredBuffer( uint32_t slot, size_t numElements, size_t elementSize, const void* bufferData )
{
    size_t bufferSize = numElements * elementSize;

    auto heapAllocation = m_UploadBuffer->Allocate( bufferSize, elementSize );

    memcpy( heapAllocation.CPU, bufferData, bufferSize );

    m_d3d12CommandList->SetGraphicsRootShaderResourceView( slot, heapAllocation.GPU );
}

void CommandList::SetGraphicsRootSignature(const RootSignature& rootSignature)
{
    auto d3d12RootSignature = rootSignature.GetRootSignature().Get();
    if (m_CurrentRootSignature != d3d12RootSignature)
    {
        m_CurrentRootSignature = d3d12RootSignature;

        for (int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
        {
            m_DynamicDescriptorHeap[i]->ParseRootSignature(rootSignature);
        }

        m_d3d12CommandList->SetGraphicsRootSignature(d3d12RootSignature);

        m_TrackedObjects.push_back(m_CurrentRootSignature);
    }
}

void CommandList::SetComputeRootSignature(const RootSignature& rootSignature)
{
    auto d3d12RootSignature = rootSignature.GetRootSignature().Get();
    if (m_CurrentRootSignature != d3d12RootSignature)
    {
        m_CurrentRootSignature = d3d12RootSignature;

        for (int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
        {
            m_DynamicDescriptorHeap[i]->ParseRootSignature(rootSignature);
        }

        m_d3d12CommandList->SetComputeRootSignature(d3d12RootSignature);

        m_TrackedObjects.push_back(m_CurrentRootSignature);
    }
}


void CommandList::SetShaderResourceView( uint32_t rootParameterIndex, uint32_t descriptorOffset, const Resource& resource, D3D12_RESOURCE_STATES stateAfter )
{
    TransitionBarrier(resource, stateAfter );

    D3D12_CPU_DESCRIPTOR_HANDLE srv[] =
    {
        resource.GetShaderResourceView()
    };

    m_DynamicDescriptorHeap[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->StageDescriptors(rootParameterIndex, descriptorOffset, 1, srv );
}

void CommandList::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t startVertex, uint32_t startInstance)
{
    FlushResourceBarriers();

    if (m_PreviousRootSignature != m_CurrentRootSignature)
    {
        m_d3d12CommandList->SetGraphicsRootSignature(m_CurrentRootSignature);
        m_PreviousRootSignature = m_CurrentRootSignature;
    }
    for (int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
    {
        m_DynamicDescriptorHeap[i]->CommitStagedDescriptorsForDraw(*this);
    }

    m_d3d12CommandList->DrawInstanced(vertexCount, instanceCount, startVertex, startInstance);
}

void CommandList::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t startIndex, int32_t baseVertex, uint32_t startInstance )
{
    FlushResourceBarriers();

    if (m_PreviousRootSignature != m_CurrentRootSignature)
    {
        m_d3d12CommandList->SetGraphicsRootSignature(m_CurrentRootSignature);
        m_PreviousRootSignature = m_CurrentRootSignature;
    }
    for (int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
    {
        m_DynamicDescriptorHeap[i]->CommitStagedDescriptorsForDraw(*this);
    }

    m_d3d12CommandList->DrawIndexedInstanced(indexCount, instanceCount, startIndex, baseVertex, startInstance );
}

bool CommandList::Close(CommandList& pendingCommandList)
{
    // Flush any remaining barriers.
    FlushResourceBarriers();

    m_d3d12CommandList->Close();

    // Flush pending resource barriers.
    uint32_t numPendingBarriers = m_ResourceStateTracker->FlushPendingResourceBarriers(pendingCommandList);
    // Commit the final resource state to the global state.
    m_ResourceStateTracker->CommitFinalResourceStates();

    return numPendingBarriers > 0;
}

void CommandList::Close()
{
    FlushResourceBarriers();
    m_d3d12CommandList->Close();
}


void CommandList::Reset()
{
    ThrowIfFailed(m_d3d12CommandAllocator->Reset());
    ThrowIfFailed(m_d3d12CommandList->Reset(m_d3d12CommandAllocator.Get(), nullptr));

    m_ResourceStateTracker->Reset();
    m_UploadBuffer->Reset();
    m_TrackedObjects.clear();

    for (int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
    {
        m_DynamicDescriptorHeap[i]->Reset();
        m_DescriptorHeaps[i] = nullptr;
    }

    m_CurrentRootSignature = nullptr;
}


void CommandList::SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, ID3D12DescriptorHeap* heap)
{
    if (m_DescriptorHeaps[heapType] != heap)
    {
        m_DescriptorHeaps[heapType] = heap;
        BindDescriptorHeaps();
    }
}

void CommandList::BindDescriptorHeaps()
{
    UINT numDescriptorHeaps = 0;
    ID3D12DescriptorHeap* descriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES] = {};

    for (uint32_t i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
    {
        ID3D12DescriptorHeap* descriptorHeap = m_DescriptorHeaps[i];
        if (descriptorHeap)
        {
            descriptorHeaps[numDescriptorHeaps++] = descriptorHeap;
        }
    }

    m_d3d12CommandList->SetDescriptorHeaps(numDescriptorHeaps, descriptorHeaps);
}

