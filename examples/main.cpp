#include "quix_commands.hpp"
#include "quix_common.hpp"
#include "quix_descriptor.hpp"
#include "quix_instance.hpp"
#include "quix_pipeline.hpp"
#include "quix_render_target.hpp"
#include "quix_resource.hpp"
#include "quix_window.hpp"

static constexpr int WIDTH = 800;
static constexpr int HEIGHT = 600;
static constexpr int FRAMES_IN_FLIGHT = 2;

struct vec3 {
    union {
        struct {
            float x, y, z;
        };
        float data[3];
    };
    NODISCARD constexpr inline float operator[](int index) const noexcept { return data[index]; }
};

struct Vertex {
    vec3 pos;
    vec3 color;

    static constexpr std::array<VkVertexInputBindingDescription, 1> get_binding_description()
    {
        return {
            VkVertexInputBindingDescription {
                .binding = 0,
                .stride = sizeof(Vertex),
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX }
        };
    }

    static constexpr std::array<VkVertexInputAttributeDescription, 2> get_attribute_description()
    {
        return {
            VkVertexInputAttributeDescription {
                .location = 0,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = offsetof(Vertex, pos) },
            VkVertexInputAttributeDescription {
                .location = 1,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = offsetof(Vertex, color) }
        };
    }
};

int main()
{
    quix::instance instance("quix_example",
        VK_MAKE_VERSION(0, 0, 1),
        WIDTH, HEIGHT);

    instance.create_device({ VK_KHR_SWAPCHAIN_EXTENSION_NAME },
        {});

    instance.create_swapchain(FRAMES_IN_FLIGHT, VK_PRESENT_MODE_FIFO_KHR);

    auto vertices = quix::create_auto_array<Vertex>(
        Vertex { vec3 { 0.0f, -0.5f, 0.0f }, vec3 { 1.0f, 1.0f, 1.0f } },
        Vertex { vec3 { 0.5f, 0.5f, 0.0f }, vec3 { 0.0f, 1.0f, 0.0f } },
        Vertex { vec3 { -0.5f, 0.5f, 0.0f }, vec3 { 0.0f, 0.0f, 1.0f } });

    auto indices = quix::create_auto_array<uint16_t>(
        0, 1, 2);

    auto vertex_buffer = instance.create_buffer_handle();
    vertex_buffer.create_staged_buffer(sizeof(Vertex) * vertices.size(),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        vertices.data(),
        &instance);

    auto index_buffer = instance.create_buffer_handle();
    index_buffer.create_staged_buffer(sizeof(uint16_t) * indices.size(),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        indices.data(),
        &instance);

    quix::renderpass_info<1, 1, 1> renderpass_info {};
    renderpass_info.attachments[0].format = instance.get_surface_format().format;
    renderpass_info.attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    renderpass_info.attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    renderpass_info.attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    renderpass_info.attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    renderpass_info.attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    renderpass_info.attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    renderpass_info.attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    renderpass_info.attachment_references[0].attachment = 0;
    renderpass_info.attachment_references[0].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    renderpass_info.subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    renderpass_info.subpasses[0].colorAttachmentCount = 1;
    renderpass_info.subpasses[0].pColorAttachments = renderpass_info.attachment_references.data();

    renderpass_info.subpass_dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    renderpass_info.subpass_dependencies[0].dstSubpass = 0;
    renderpass_info.subpass_dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    renderpass_info.subpass_dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    renderpass_info.subpass_dependencies[0].srcAccessMask = 0;
    renderpass_info.subpass_dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    renderpass_info.subpass_dependencies[0].dependencyFlags = 0;

    auto render_target = instance.create_render_target(renderpass_info.export_renderpass_info());

    instance.create_pipeline_manager();
    auto pipeline_manager = instance.get_pipeline_manager();
    auto pipeline_builder = pipeline_manager->create_pipeline_builder(&render_target);

    auto vertex_binding_description = Vertex::get_binding_description();
    auto vertex_attribute_description = Vertex::get_attribute_description();

    auto shader_stages = pipeline_builder.create_shader_array(
        pipeline_builder.create_shader_stage("examples/simpleshader.vert", VK_SHADER_STAGE_VERTEX_BIT),
        pipeline_builder.create_shader_stage("examples/simpleshader.frag", VK_SHADER_STAGE_FRAGMENT_BIT));

    auto allocator_pool = instance.get_descriptor_allocator_pool();
    auto descriptor_set_builder = instance.get_descriptor_builder(&allocator_pool);
    auto descriptor_set_layout = descriptor_set_builder
                                     .bind_buffer(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
                                     .buildLayout();

    auto pipeline = pipeline_builder.add_shader_stages(shader_stages)
                        .create_vertex_state(vertex_binding_description.data(), vertex_binding_description.size(), vertex_attribute_description.data(), vertex_attribute_description.size())
                        .add_push_constant(VK_SHADER_STAGE_VERTEX_BIT, sizeof(float) * 4)
                        .add_descriptor_set_layout(descriptor_set_layout)
                        .create_graphics_pipeline();

    auto command_pool = instance.get_command_pool();

    auto window = instance.get_window();

    int current_frame = 0;
    uint32_t current_image_index = 0;

    auto sync_objects = instance.create_sync_objects();

    std::array<quix::allocated_unique_ptr<quix::command_list>, FRAMES_IN_FLIGHT> command_lists {
        command_pool.create_command_list(), command_pool.create_command_list()
    };

    std::array<VkClearValue, 1> clear_values = {
        { { 0.0f, 0.0f, 0.0f, 0.0f } }
    };

    std::array<VkBuffer, 1> vertex_buffer_array = { vertex_buffer.get_buffer() };
    std::array<VkDeviceSize, 1> offsets = { 0 };

    while (!window->should_close()) {
        window->poll_events();

        VkResult result = sync_objects.acquire_next_image(current_frame, &current_image_index);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            render_target.recreate_swapchain();
            continue;
        } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            quix_error("failed to acquire swapchain image");
        }
        sync_objects.reset_fence(current_frame);

        command_lists[current_frame]->begin_record();

        command_lists[current_frame]->begin_render_pass(render_target, pipeline, current_image_index, clear_values.data(), clear_values.size());

        vkCmdBindVertexBuffers(command_lists[current_frame]->get_cmd_buffer(), 0, vertex_buffer_array.size(), vertex_buffer_array.data(), offsets.data());

        vkCmdBindIndexBuffer(command_lists[current_frame]->get_cmd_buffer(), index_buffer.get_buffer(), 0, VK_INDEX_TYPE_UINT16);

        vkCmdDrawIndexed(command_lists[current_frame]->get_cmd_buffer(), index_buffer.get_alloc_info().size/sizeof(uint16_t), 1, 0, 0, 0);

        command_lists[current_frame]->end_render_pass();

        command_lists[current_frame]->end_record();

        VK_CHECK(sync_objects.submit_frame(current_frame, command_lists[current_frame].get()), "failed to submit frame");

        result = sync_objects.present_frame(current_frame, current_image_index);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || window->get_framebuffer_resized()) {
            render_target.recreate_swapchain();
        } else if (result != VK_SUCCESS) {
            quix_error("failed to present swapchain image");
        }
        current_frame = (current_frame + 1) % FRAMES_IN_FLIGHT;
    }

    instance.wait_idle();

    return 0;
}