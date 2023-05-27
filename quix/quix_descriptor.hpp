#ifndef _QUIX_DESCRIPTOR_HPP
#define _QUIX_DESCRIPTOR_HPP

namespace quix {

namespace descriptor {

    class allocator;

    struct allocator_pool {
        friend class allocator;
        friend class builder;
    public:
        allocator_pool(class allocator* allocator, VkDescriptorPool pool);
        ~allocator_pool();

        allocator_pool(const allocator_pool&) = delete;
        const allocator_pool& operator=(allocator_pool const&) = delete;
        allocator_pool(allocator_pool&& other) = delete;
        allocator_pool& operator=(allocator_pool&& other) = delete;

        void returnPool();
    private:
        VkDescriptorSet allocate(VkDescriptorSetLayout layout);

        allocator* allocator { nullptr };
        std::deque<VkDescriptorPool> usedPools;
        VkDescriptorPool currentPool { VK_NULL_HANDLE };
    };

    class allocator {
        friend class allocator_pool;
    public:
        static allocator* init(VkDevice device);
        void cleanup();
        VkDevice getDevice();
        allocator_pool getPool();
        ~allocator();

    private:
        allocator();
        allocator(const allocator&) = delete;
        void operator=(allocator const&) = delete;

        VkDevice device { VK_NULL_HANDLE };

        VkDescriptorPool createDescriptorPool(int count, VkDescriptorPoolCreateFlags flags);

        std::mutex poolMutex;
        std::deque<VkDescriptorPool> availablePools;

        VkDescriptorPool borrowPool();
        void returnPool(allocator_pool& pool);
    };

    class layout_cache {
    public:
        void init(VkDevice newDevice);
        void cleanup();

        ~layout_cache();
        layout_cache& operator=(const layout_cache&) = delete;
        layout_cache& operator=(layout_cache&&) = delete;

        // unordered map is sychronized
        VkDescriptorSetLayout create_descriptor_layout(VkDescriptorSetLayoutCreateInfo* info);

        struct DescriptorLayoutInfo {
            // good idea to turn this into an inlined array
            std::vector<VkDescriptorSetLayoutBinding> bindings;

            bool operator==(const DescriptorLayoutInfo& other) const;

            size_t hash() const;
        };

    private:
        struct DescriptorLayoutHash {
            std::size_t operator()(const DescriptorLayoutInfo& k) const
            {
                return k.hash();
            }
        };

        std::mutex layoutCacheMutex;
        std::unordered_map<DescriptorLayoutInfo, VkDescriptorSetLayout, DescriptorLayoutHash> layoutCache;
        VkDevice device;
    };

    class builder {
    public:
        static builder begin(layout_cache* layoutCache, allocator_pool* pool);

        void bind_buffer(uint32_t binding, VkDescriptorType type, VkShaderStageFlags stageFlags);
        void bind_image(uint32_t binding, VkDescriptorType type, VkShaderStageFlags stageFlags);

        void update_buffer(uint32_t binding, VkDescriptorBufferInfo* bufferInfo);
        void update_image(uint32_t binding, VkDescriptorImageInfo* imageInfo);

        VkDescriptorSetLayout buildLayout();

        VkDescriptorSet buildSet();

    private:
        std::vector<VkWriteDescriptorSet> writes;
        std::vector<VkDescriptorSetLayoutBinding> bindings;

        VkDescriptorSetLayout layout;

        layout_cache* cache;
        allocator_pool* alloc;
    };

} // namespace descriptor

} // namespace quix

#endif // _QUIX_DESCRIPTOR_HPP
