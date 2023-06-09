#ifndef _QUIX_DESCRIPTOR_CPP
#define _QUIX_DESCRIPTOR_CPP

#include "quix_descriptor.hpp"

namespace quix::descriptor {

    // allocator_pool struct start

    allocator_pool::allocator_pool(quix::descriptor::allocator* p_allocator, VkDescriptorPool pool)
        : m_allocator(p_allocator)
        , currentPool(pool)
    {
    }

    allocator_pool::~allocator_pool()
    {
        returnPool();
    }

    void allocator_pool::returnPool()
    {
        if (currentPool == VK_NULL_HANDLE) {
            spdlog::warn("Pool already freed");
            return;
        }
        m_allocator->returnPool(*this);
    }

    VkDescriptorSet allocator_pool::allocate(VkDescriptorSetLayout layout)
    {
        VkDescriptorSet set;
        if (currentPool == VK_NULL_HANDLE) {
            currentPool = m_allocator->borrowPool();
        }

        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.pNext = nullptr;

        allocInfo.pSetLayouts = &layout;
        allocInfo.descriptorPool = currentPool;
        allocInfo.descriptorSetCount = 1;

        // try to allocate the descriptor set
        VkResult allocResult = vkAllocateDescriptorSets(m_allocator->device, &allocInfo, &set);
        bool needReallocate = false;

        switch (allocResult) {
        case VK_SUCCESS:
            // all good, return
            return set;
        case VK_ERROR_FRAGMENTED_POOL:
        case VK_ERROR_OUT_OF_POOL_MEMORY:
            // reallocate pool
            needReallocate = true;
            break;
        default:
            // unrecoverable error
            quix_error("failed to allocate descriptor set (unrecoverable error)");
        }

        if (needReallocate) {
            // allocate a new pool and retry
            usedPools.push_back(currentPool);
            currentPool = m_allocator->borrowPool();

            allocResult = vkAllocateDescriptorSets(m_allocator->device, &allocInfo, &set);

            // if it still fails then we have big issues
            if (allocResult == VK_SUCCESS) {
                return set;
            }
        }

        quix_error("Failed to allocate descriptor set (after realloc)");
        // I had to do this to get rid of a warning even though quix_error exits the program
        return {};
    }

    // allocator_pool struct end

    // allocator class start

    static constexpr struct PoolSizes {
        std::array<std::pair<VkDescriptorType, float>, 11> sizes = { { { VK_DESCRIPTOR_TYPE_SAMPLER, 0.5f },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4.f },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4.f },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1.f },
            { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1.f },
            { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1.f },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2.f },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2.f },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1.f },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1.f },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 0.5f } } };
    } s_PoolSizes;

    allocator::allocator(VkDevice device)
        : device(device)
    {
    }

    VkDevice allocator::getDevice()
    {
        return device;
    }

    allocator_pool allocator::getPool()
    {
        quix_assert(device != VK_NULL_HANDLE, "allocator class was not initialized (device is VK_NULL_HANDLE)");

        return allocator_pool(this, borrowPool());
    }

    allocator::~allocator()
    {
        while (availablePools.size() > 0) {
            vkDestroyDescriptorPool(device, availablePools.front(), nullptr);
            availablePools.pop_front();
        }
    }

    VkDescriptorPool allocator::createDescriptorPool(int count, VkDescriptorPoolCreateFlags flags)
    {
        auto* sizes = (VkDescriptorPoolSize*)alloca(s_PoolSizes.sizes.size() * sizeof(VkDescriptorPoolSize));

        for (std::size_t i = 0; i < s_PoolSizes.sizes.size(); i++) {
            sizes[i] = { s_PoolSizes.sizes[i].first, static_cast<uint32_t>(s_PoolSizes.sizes[i].second * count) };
        }

        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = flags;
        pool_info.maxSets = count;
        pool_info.poolSizeCount = static_cast<uint32_t>(s_PoolSizes.sizes.size());
        pool_info.pPoolSizes = sizes;

        VkDescriptorPool descriptorPool;
        vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptorPool);

        return descriptorPool;
    }

    VkDescriptorPool allocator::borrowPool()
    {
        VkDescriptorPool currentPool;

        if (availablePools.size() == 0) {
            currentPool = createDescriptorPool(1000, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT); // TODO maybe set to framesinflight? idk
        } else {
            std::lock_guard<std::mutex> lock(poolMutex);
            currentPool = availablePools.front();
            availablePools.pop_front();
        }

        return currentPool;
    }

    void allocator::returnPool(allocator_pool& pool)
    {
        vkResetDescriptorPool(device, pool.currentPool, 0); // TODO might be worth it to just pop these off since I am using a deque instead of this optimization
        for (auto& d_pool : pool.usedPools) {
            vkResetDescriptorPool(device, d_pool, 0);
        }
        std::lock_guard<std::mutex> lock(poolMutex);

        availablePools.push_back(pool.currentPool);
        pool.currentPool = VK_NULL_HANDLE;
        for (auto& d_pool : pool.usedPools) {
            availablePools.push_back(d_pool);
        }
        pool.usedPools.clear();
    }

    // allocator class end

    // layoutcache class start

    layout_cache::layout_cache(VkDevice device)
        : device(device)
    {
    }

    layout_cache::~layout_cache()
    {
        cleanup();
    }

    void layout_cache::cleanup()
    {
        // delete every descriptor layout held
        for (const auto& pair : layoutCache) {
            vkDestroyDescriptorSetLayout(device, pair.second, nullptr);
        }
    }

    VkDescriptorSetLayout layout_cache::create_descriptor_layout(VkDescriptorSetLayoutCreateInfo* info)
    {
        descriptor_layout_info layoutinfo;
        layoutinfo.bindings.reserve(info->bindingCount);
        bool isSorted = true;
        uint32_t lastBinding = -1;

        // copy from the direct info struct into our own one
        for (uint32_t i = 0; i < info->bindingCount; i++) {
            layoutinfo.bindings.push_back(info->pBindings[i]);

            // check that the bindings are in strict increasing order
            if (info->pBindings[i].binding > lastBinding) {
                lastBinding = info->pBindings[i].binding;
            } else {
                isSorted = false;
            }
        }
        // sort the bindings if they aren't in order
        if (!isSorted) {
            std::sort(layoutinfo.bindings.begin(), layoutinfo.bindings.end(), [](VkDescriptorSetLayoutBinding& lhs, VkDescriptorSetLayoutBinding& rhs) {
                return lhs.binding < rhs.binding;
            });
        }

        // try to grab from cache
        layoutCacheMutex.lock();
        auto it = layoutCache.find(layoutinfo);
        if (it != layoutCache.end()) {
            layoutCacheMutex.unlock();
            return (*it).second;
        } else {
            // create a new one (not found)
            VkDescriptorSetLayout layout = VK_NULL_HANDLE;
            vkCreateDescriptorSetLayout(device, info, nullptr, &layout);

            // add to cache
            layoutCache[layoutinfo] = layout;
            layoutCacheMutex.unlock();
            return layout;
        }
    }

    bool layout_cache::descriptor_layout_info::operator==(const descriptor_layout_info& other) const
    {
        if (other.bindings.size() != bindings.size()) {
            return false;
        } else {
            // compare each of the bindings is the same. Bindings are sorted so they will match
            for (std::size_t i = 0; i < bindings.size(); i++) {
                if (other.bindings[i].binding != bindings[i].binding) {
                    return false;
                }
                if (other.bindings[i].descriptorType != bindings[i].descriptorType) {
                    return false;
                }
                if (other.bindings[i].descriptorCount != bindings[i].descriptorCount) {
                    return false;
                }
                if (other.bindings[i].stageFlags != bindings[i].stageFlags) {
                    return false;
                }
            }
            return true;
        }
    }

    size_t layout_cache::descriptor_layout_info::hash() const
    {
        using std::hash;
        using std::size_t;

        size_t result = hash<size_t>()(bindings.size());

        for (const VkDescriptorSetLayoutBinding& b : bindings) {
            // pack the binding data into a single int64. Not fully correct but it's ok
            size_t binding_hash = b.binding | b.descriptorType << 8 | b.descriptorCount << 16 | b.stageFlags << 24;

            // shuffle the packed binding data and xor it with the main hash
            result ^= hash<size_t>()(binding_hash);
        }

        return result;
    }

    // layoutcache class end

    // builder class start

    builder::builder(layout_cache* layoutCache, allocator_pool* pool)
        : layout(VK_NULL_HANDLE)
        , cache(layoutCache)
        , alloc(pool)
    {
    }

    builder& builder::bind_buffer(uint32_t binding, VkDescriptorType type, VkShaderStageFlags stageFlags)
    {
        // create the descriptor binding for the layout
        VkDescriptorSetLayoutBinding newBinding {};

        newBinding.descriptorCount = 1;
        newBinding.descriptorType = type;
        newBinding.pImmutableSamplers = nullptr;
        newBinding.stageFlags = stageFlags;
        newBinding.binding = binding;

        bindings.push_back(newBinding);

        // create the descriptor write
        VkWriteDescriptorSet newWrite {};
        newWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        newWrite.pNext = nullptr;

        newWrite.descriptorCount = 1;
        newWrite.descriptorType = type;
        newWrite.pBufferInfo = nullptr;
        newWrite.dstBinding = binding;

        writes.push_back(newWrite);

        return *this;
    }

    builder& builder::bind_image(uint32_t binding, VkDescriptorType type, VkShaderStageFlags stageFlags)
    {
        // create the descriptor binding for the layout
        VkDescriptorSetLayoutBinding newBinding {};

        newBinding.descriptorCount = 1;
        newBinding.descriptorType = type;
        newBinding.pImmutableSamplers = nullptr;
        newBinding.stageFlags = stageFlags;
        newBinding.binding = binding;

        bindings.push_back(newBinding);

        // create the descriptor write
        VkWriteDescriptorSet newWrite {};
        newWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        newWrite.pNext = nullptr;

        newWrite.descriptorCount = 1;
        newWrite.descriptorType = type;
        newWrite.pImageInfo = nullptr;
        newWrite.dstBinding = binding;

        writes.push_back(newWrite);

        return *this;
    }

    builder& builder::update_buffer(uint32_t binding, VkDescriptorBufferInfo* bufferInfo)
    {
        writes[binding].pBufferInfo = bufferInfo;

        return *this;
    }

    builder& builder::update_image(uint32_t binding, VkDescriptorImageInfo* imageInfo)
    {
        writes[binding].pImageInfo = imageInfo;

        return *this;
    }

    VkDescriptorSetLayout builder::buildLayout()
    {
        VkDescriptorSetLayoutCreateInfo layoutInfo {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.pNext = nullptr;

        layoutInfo.pBindings = bindings.data();
        layoutInfo.bindingCount = bindings.size();

        layout = cache->create_descriptor_layout(&layoutInfo);

        return layout;
    }

    VkDescriptorSet builder::buildSet()
    {

        // allocate descriptor
        VkDescriptorSet set = alloc->allocate(layout);

        // write descriptor
        for (VkWriteDescriptorSet& w : writes) {
            w.dstSet = set;
        }

        vkUpdateDescriptorSets(alloc->m_allocator->getDevice(), writes.size(), writes.data(), 0, nullptr);

        return set;
    }

    // builder class end

} // namespace quix

#endif // _QUIX_DESCRIPTOR_CPP