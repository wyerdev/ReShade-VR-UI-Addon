#pragma once

// The game's Vulkan loader is used through function pointers, so Direct3D and OpenGL games never load Vulkan because of this add-on.
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace openxr_ui
{
	// What the Dear ImGui Vulkan backend needs from the game's OpenXR session.
	struct vulkan_renderer_setup
	{
		VkInstance instance = VK_NULL_HANDLE;
		VkPhysicalDevice physical_device = VK_NULL_HANDLE;
		VkDevice device = VK_NULL_HANDLE;
		uint32_t queue_family = 0;
		VkQueue queue = VK_NULL_HANDLE;
		VkRenderPass render_pass = VK_NULL_HANDLE;
		// Menu drawings that may be in flight on the GPU at once.
		uint32_t frame_count = 0;
		PFN_vkGetInstanceProcAddr get_instance_proc_addr = nullptr;
	};

	// The images of one menu swapchain, ready to be drawn into.
	struct vulkan_quad_images
	{
		std::vector<VkImageView> views;
		std::vector<VkFramebuffer> framebuffers;
		uint32_t width = 0;
		uint32_t height = 0;
	};

	// Draws the menu with the Vulkan device and queue the game gave to OpenXR. The game owns those; this only owns what it creates.
	// The queue is used only inside xrEndFrame, where OpenXR requires the game to keep its other threads off that queue.
	class vulkan_quad_device
	{
	public:
		// Menu submissions that may be in flight on the GPU before the next one has to skip a frame.
		static constexpr uint32_t frame_slot_count = 4;

		static std::unique_ptr<vulkan_quad_device> create(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device,
			uint32_t queue_family, uint32_t queue_index);
		~vulkan_quad_device();

		vulkan_quad_device(const vulkan_quad_device &) = delete;
		vulkan_quad_device &operator=(const vulkan_quad_device &) = delete;

		bool create_render_pass(VkFormat format);
		vulkan_renderer_setup renderer_setup(uint32_t image_count) const;
		bool create_images(const std::vector<VkImage> &images, VkFormat format, uint32_t width, uint32_t height, vulkan_quad_images &target);
		void destroy_images(vulkan_quad_images &target);

		// Only reads the GPU's progress; when the next command buffer is still in use the menu skips a frame instead of waiting.
		bool slot_free();
		// Starts drawing into that image, cleared to transparent. Null when the commands could not be started.
		VkCommandBuffer begin_commands(const vulkan_quad_images &target, uint32_t image_index);
		bool submit_commands();
		// Called once when the session ends, so nothing is destroyed while the GPU still draws with it.
		void wait_for_submitted_commands();

	private:
		struct functions
		{
			PFN_vkGetDeviceQueue GetDeviceQueue = nullptr;
			PFN_vkCreateRenderPass CreateRenderPass = nullptr;
			PFN_vkDestroyRenderPass DestroyRenderPass = nullptr;
			PFN_vkCreateImageView CreateImageView = nullptr;
			PFN_vkDestroyImageView DestroyImageView = nullptr;
			PFN_vkCreateFramebuffer CreateFramebuffer = nullptr;
			PFN_vkDestroyFramebuffer DestroyFramebuffer = nullptr;
			PFN_vkCreateCommandPool CreateCommandPool = nullptr;
			PFN_vkDestroyCommandPool DestroyCommandPool = nullptr;
			PFN_vkAllocateCommandBuffers AllocateCommandBuffers = nullptr;
			PFN_vkResetCommandBuffer ResetCommandBuffer = nullptr;
			PFN_vkBeginCommandBuffer BeginCommandBuffer = nullptr;
			PFN_vkEndCommandBuffer EndCommandBuffer = nullptr;
			PFN_vkCmdBeginRenderPass CmdBeginRenderPass = nullptr;
			PFN_vkCmdEndRenderPass CmdEndRenderPass = nullptr;
			PFN_vkCreateFence CreateFence = nullptr;
			PFN_vkDestroyFence DestroyFence = nullptr;
			PFN_vkGetFenceStatus GetFenceStatus = nullptr;
			PFN_vkResetFences ResetFences = nullptr;
			PFN_vkWaitForFences WaitForFences = nullptr;
			PFN_vkQueueSubmit QueueSubmit = nullptr;
		};

		struct frame_slot
		{
			VkCommandBuffer commands = VK_NULL_HANDLE;
			VkFence fence = VK_NULL_HANDLE;
			bool submitted = false;
		};

		vulkan_quad_device() = default;
		bool load_functions();
		bool create_frame_slots();

		PFN_vkGetInstanceProcAddr _get_instance_proc_addr = nullptr;
		functions _vk;
		VkInstance _instance = VK_NULL_HANDLE;
		VkPhysicalDevice _physical_device = VK_NULL_HANDLE;
		VkDevice _device = VK_NULL_HANDLE;
		uint32_t _queue_family = 0;
		VkQueue _queue = VK_NULL_HANDLE;
		VkRenderPass _render_pass = VK_NULL_HANDLE;
		VkCommandPool _command_pool = VK_NULL_HANDLE;
		std::array<frame_slot, frame_slot_count> _frame_slots;
		size_t _next_frame_slot = 0;
	};
}
