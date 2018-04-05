/**
 * The DynamicDescriptorHeap is a GPU visible descriptor heap that allows for
 * staging of CPU visible descriptors that need to be uploaded before a Draw
 * or Dispatch command is executed.
 * The DynamicDescriptorHeap class is based on the one provided by the MiniEngine:
 * https://github.com/Microsoft/DirectX-Graphics-Samples
 */

#include <d3d12.h>

#include <memory>

class CommandList;

class DynamicDescriptorHeap
{
public:
    DynamicDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE heapType,
        D3D12_COMMAND_LIST_TYPE commandListType,
        uint32_t numDescriptorsPerHeap = 1024);

    virtual ~DynamicDescriptorHeap();

    /**
     * Stages a contiguous range of CPU visible descriptors.
     * Descriptors are not copied to the GPU visible descriptor heap until
     * the CopyAndBindStagedDescriptors function is called.
     */
    void StageDescriptors(uint32_t rootIndexParameterIndex, uint32_t offset, uint32_t numDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptor);

    /**
     * Copies a single CPU visible descriptor to a GPU visible descriptor heap.
     * This is useful for the
     *   * ID3D12GraphicsCommandList::ClearUnorderedAccessViewFloat
     *   * ID3D12GraphicsCommandList::ClearUnorderedAccessViewUint
     * methods which which require both a CPU and GPU visible descriptors for a 
     * UAV resource.
     * 
     * @param cpuDescriptor The CPU descriptor to copy into a GPU visible 
     * descriptor heap.
     * 
     * @return The GPU visible descriptor.
     */
    D3D12_GPU_DESCRIPTOR_HANDLE CopyDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor);

    /**
     * Copy all of the staged descriptors to the GPU visible descriptor heap and
     * bind the descriptor heap and the descriptor tables to the command list.
     */
    void CopyAndBindStagedDescriptors(std::shared_ptr<CommandList> commandList);
protected:

private:


    /**
     * The maximum number of descriptor tables per root signature.
     * A 32-bit mask is used to keep track of the root parameter indices that
     * are descriptor tables.
     */
    constexpr uint32_t MaxDescriptorTables = 32;

    /**
     * A structure that represents a descriptor table entry in the root signature.
     */
    struct DescriptorTableCache
    {
        DescriptorTableCache()
            : NumDescriptors(0)
            , BaseDescriptor(nullptr)
            , NumDescriptorRanges(0)
        {}

        // The number of descriptors in this descriptor table.
        uint32_t NumDescriptors;
        // The pointer to the descriptor in the descriptor handle cache.
        D3D12_CPU_DESCRIPTOR_HANDLE* BaseDescriptor;
    };

    // Describe the type of command list being used to stage the descriptors.
    // Valid values are:
    //   * D3D12_COMMAND_LIST_TYPE_DIRECT
    //   * D3D12_COMMAND_LIST_TYPE_COMPUTE
    D3D12_COMMAND_LIST_TYPE m_CommandListType;

    // Describes the type of descriptors that can be staged using this 
    // dynamic descriptor heap.
    // Valid values are:
    //   * D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
    //   * D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
    // This parameter also determines the type of GPU visible descriptor heap to 
    // create.
    D3D12_DESCRIPTOR_HEAP_TYPE m_DescriptorHeapType;

    // The descriptor handle cache.
    std::unique_ptr<D3D12_CPU_DESCRIPTOR_HANDLE[]> m_DescriptorHandleCache;

    // Descriptor handle cache per descriptor table.
    DescriptorTableCache m_DescriptorTableCache[MaxDescriptorTables];
    // Each bit set in the bit mask represents a descriptor table
    // in the root signature that needs to be bound to the command list
    // before rendering or dispatch.
    uint32_t m_RootDescriptorTableBitMask;
};