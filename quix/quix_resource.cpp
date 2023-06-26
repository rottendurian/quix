#ifndef _QUIX_RESOURCE_CPP
#define _QUIX_RESOURCE_CPP

#include "quix_resource.hpp"

#include "quix_commands.hpp"
#include "quix_device.hpp"
#include "quix_instance.hpp"
#include <cstdint>
#include <vulkan/vulkan_core.h>

namespace quix {

buffer_handle::buffer_handle(weakref<device> p_device)
    : m_device(std::move(p_device))
{
}

buffer_handle::~buffer_handle()
{
    if (m_buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_device->get_allocator(), m_buffer, m_alloc);
    } else {
        spdlog::warn("buffer was never created");
    }
}

void buffer_handle::create_buffer(const VkBufferCreateInfo* create_info, const VmaAllocationCreateInfo* alloc_info)
{
    VK_CHECK(vmaCreateBuffer(m_device->get_allocator(), create_info, alloc_info, &m_buffer, &m_alloc, &m_alloc_info), "failed to create buffer");
}

void buffer_handle::create_uniform_buffer(const VkDeviceSize size)
{
    VkBufferCreateInfo buffer_info {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_info {};
    alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;

    create_buffer(&buffer_info, &alloc_info);
}

void buffer_handle::create_cpu_buffer(const VkDeviceSize size, const VkBufferUsageFlags usage_flags, const VmaAllocationCreateFlagBits alloc_flags)
{
    VkBufferCreateInfo buffer_info {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage_flags;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_info {};
    alloc_info.flags = alloc_flags;
    alloc_info.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    create_buffer(&buffer_info, &alloc_info);
}

void buffer_handle::create_gpu_buffer(const VkDeviceSize size, const VkBufferUsageFlags usage_flags)
{
    VkBufferCreateInfo buffer_info {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage_flags;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_info {};
    alloc_info.flags = 0;
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    create_buffer(&buffer_info, &alloc_info);
}

void buffer_handle::create_staged_buffer(const VkDeviceSize size, const VkBufferUsageFlags usage_flags, const void* data, instance* inst)
{
    buffer_handle staging_buffer(m_device);
    staging_buffer.create_staging_buffer(size);
    memcpy(staging_buffer.get_mapped_data(), data, size);

    VkBufferCreateInfo buffer_info {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage_flags;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_info {};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    create_buffer(&buffer_info, &alloc_info);

    auto cmd_pool = inst->get_command_pool();
    auto cmd_list = cmd_pool.create_command_list();

    cmd_list->begin_record(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    cmd_list->copy_buffer_to_buffer(staging_buffer.get_buffer(), 0, m_buffer, 0, size);
    cmd_list->end_record();
    VkFence fence = inst->create_fence();
    cmd_list->submit(fence);
    vkWaitForFences(m_device->get_logical_device(), 1, &fence, VK_TRUE, UINT64_MAX); // hopefully no deadlocking
    vkDestroyFence(m_device->get_logical_device(), fence, nullptr);
}

void buffer_handle::create_staging_buffer(const VkDeviceSize size)
{
    VkBufferCreateInfo buffer_info {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_info {};
    alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    alloc_info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    create_buffer(&buffer_info, &alloc_info);
}

image_handle::image_handle(weakref<device> p_device)
    : m_device(std::move(p_device))
{
}

image_handle::~image_handle()
{
    if (m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device->get_logical_device(), m_sampler, nullptr);
    }

    if (m_view != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device->get_logical_device(), m_view, nullptr);
    }

    if (m_image != VK_NULL_HANDLE) {
        vmaDestroyImage(m_device->get_allocator(), m_image, m_alloc);
    } else {
        spdlog::warn("image was never created");
    }
}

void image_handle::create_image(const VkImageCreateInfo* create_info, const VmaAllocationCreateInfo* alloc_info)
{
    m_type = create_info->imageType;
    m_format = create_info->format;
    m_mip_levels = create_info->mipLevels;
    m_array_layers = create_info->arrayLayers;
    m_samples = create_info->samples;
    m_extent = create_info->extent;
    VK_CHECK(vmaCreateImage(m_device->get_allocator(), create_info, alloc_info, &m_image, &m_alloc, &m_alloc_info), "failed to create image");
}

void image_handle::create_image_from_file(const char* filepath, instance* inst)
{
    int texture_width{};
    int texture_height{};
    int texture_channels{};
    stbi_uc* pixels = stbi_load(filepath, &texture_width, &texture_height, &texture_channels, STBI_rgb_alpha);
    auto texture_size = (VkDeviceSize)(texture_width * texture_height * 4);

    auto buffer_handle = inst->create_buffer_handle();
    buffer_handle.create_staging_buffer(texture_size);
    std::memcpy(buffer_handle.get_mapped_data(), pixels, texture_size);

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent.width = texture_width;
    image_info.extent.height = texture_height;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = VK_FORMAT_R8G8B8A8_SRGB;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    alloc_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    create_image(&image_info, &alloc_info);

    auto cmd_pool = inst->get_command_pool();
    auto cmd_list = cmd_pool.create_command_list();

    cmd_list->begin_record(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    image_barrier_info barrier_info{};
    barrier_info.src_access_mask = 0;
    barrier_info.dst_access_mask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier_info.src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    barrier_info.dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    barrier_info.old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier_info.new_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    cmd_list->image_barrier(this, &barrier_info);

    cmd_list->copy_buffer_to_image(buffer_handle.get_buffer(), 0, this, {0, 0, 0});

    barrier_info.src_access_mask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier_info.dst_access_mask = VK_ACCESS_SHADER_READ_BIT;
    barrier_info.src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    barrier_info.dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    barrier_info.old_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier_info.new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    cmd_list->image_barrier(this, &barrier_info);
    
    cmd_list->end_record();

    VkFence fence = inst->create_fence();
    cmd_list->submit(fence);
    vkWaitForFences(m_device->get_logical_device(), 1, &fence, VK_TRUE, UINT64_MAX); // hopefully no deadlocking
    vkDestroyFence(m_device->get_logical_device(), fence, nullptr);

    stbi_image_free(pixels);

}

void image_handle::create_view()
{
    VkImageViewCreateInfo create_info{};
    create_info.image = m_image;
    create_info.viewType = type_to_view_type();
    create_info.format = m_format;
    create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; // TODO might need support for other aspect masks later on? though depth buffer views are created automatically so maybe not
    create_info.subresourceRange.baseMipLevel = 0;
    create_info.subresourceRange.levelCount = m_mip_levels;
    create_info.subresourceRange.baseArrayLayer = 0;
    create_info.subresourceRange.layerCount = m_array_layers;
    create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

    VK_CHECK(vkCreateImageView(m_device->get_logical_device(), &create_info, nullptr, &m_view), "failed to create image view");
}

void image_handle::create_sampler(const VkSamplerCreateInfo* create_info)
{
    VK_CHECK(vkCreateSampler(m_device->get_logical_device(), create_info, nullptr, &m_sampler), "failed to create sampler");
}

constexpr VkImageViewType image_handle::type_to_view_type()
{
    switch (m_type) {
        case VK_IMAGE_TYPE_1D:
            return VK_IMAGE_VIEW_TYPE_1D;
        case VK_IMAGE_TYPE_2D:
            return VK_IMAGE_VIEW_TYPE_2D;
        case VK_IMAGE_TYPE_3D:
            return VK_IMAGE_VIEW_TYPE_3D;
        default:
            spdlog::warn("Unsupported type used for image");
            return VK_IMAGE_VIEW_TYPE_1D;
    }
}

} // namespace quix

#endif // _QUIX_RESOURCE_CPP
