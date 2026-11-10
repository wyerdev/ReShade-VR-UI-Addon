#include "openxr_ui_vulkan.hpp"

#include <Windows.h>

#include <algorithm>
#include <type_traits>

namespace
{
	constexpr uint64_t session_end_wait_nanoseconds = 1'000'000'000;
}

namespace openxr_ui
{
	std::unique_ptr<vulkan_quad_device> vulkan_quad_device::create(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device,
		uint32_t queue_family, uint32_t queue_index)
	{
		if (instance == VK_NULL_HANDLE || physical_device == VK_NULL_HANDLE || device == VK_NULL_HANDLE)
			return nullptr;
		// A game with a Vulkan OpenXR session has already loaded the Vulkan loader.
		const HMODULE loader = GetModuleHandleW(L"vulkan-1.dll");
		if (loader == nullptr)
			return nullptr;

		std::unique_ptr<vulkan_quad_device> result(new vulkan_quad_device());
		result->_get_instance_proc_addr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader, "vkGetInstanceProcAddr"));
		result->_instance = instance;
		result->_physical_device = physical_device;
		result->_device = device;
		result->_queue_family = queue_family;
		if (result->_get_instance_proc_addr == nullptr || !result->load_functions())
			return nullptr;
		result->_vk.GetDeviceQueue(device, queue_family, queue_index, &result->_queue);
		if (result->_queue == VK_NULL_HANDLE || !result->create_frame_slots())
			return nullptr;
		return result;
	}

	vulkan_quad_device::~vulkan_quad_device()
	{
		for (const frame_slot &slot : _frame_slots)
			if (slot.fence != VK_NULL_HANDLE)
				_vk.DestroyFence(_device, slot.fence, nullptr);
		// Frees its command buffers too.
		if (_command_pool != VK_NULL_HANDLE)
			_vk.DestroyCommandPool(_device, _command_pool, nullptr);
		if (_render_pass != VK_NULL_HANDLE)
			_vk.DestroyRenderPass(_device, _render_pass, nullptr);
	}

	bool vulkan_quad_device::load_functions()
	{
		const auto load = [this](auto &function, const char *name) {
			function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(_get_instance_proc_addr(_instance, name));
			return function != nullptr;
		};
		return load(_vk.GetDeviceQueue, "vkGetDeviceQueue") && load(_vk.CreateRenderPass, "vkCreateRenderPass") &&
			load(_vk.DestroyRenderPass, "vkDestroyRenderPass") && load(_vk.CreateImageView, "vkCreateImageView") &&
			load(_vk.DestroyImageView, "vkDestroyImageView") && load(_vk.CreateFramebuffer, "vkCreateFramebuffer") &&
			load(_vk.DestroyFramebuffer, "vkDestroyFramebuffer") && load(_vk.CreateCommandPool, "vkCreateCommandPool") &&
			load(_vk.DestroyCommandPool, "vkDestroyCommandPool") && load(_vk.AllocateCommandBuffers, "vkAllocateCommandBuffers") &&
			load(_vk.ResetCommandBuffer, "vkResetCommandBuffer") && load(_vk.BeginCommandBuffer, "vkBeginCommandBuffer") &&
			load(_vk.EndCommandBuffer, "vkEndCommandBuffer") && load(_vk.CmdBeginRenderPass, "vkCmdBeginRenderPass") &&
			load(_vk.CmdEndRenderPass, "vkCmdEndRenderPass") && load(_vk.CreateFence, "vkCreateFence") &&
			load(_vk.DestroyFence, "vkDestroyFence") && load(_vk.GetFenceStatus, "vkGetFenceStatus") &&
			load(_vk.ResetFences, "vkResetFences") && load(_vk.WaitForFences, "vkWaitForFences") &&
			load(_vk.QueueSubmit, "vkQueueSubmit");
	}

	bool vulkan_quad_device::create_frame_slots()
	{
		VkCommandPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
		pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		pool_info.queueFamilyIndex = _queue_family;
		if (_vk.CreateCommandPool(_device, &pool_info, nullptr, &_command_pool) != VK_SUCCESS)
			return false;

		std::array<VkCommandBuffer, frame_slot_count> command_buffers = {};
		VkCommandBufferAllocateInfo allocate_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		allocate_info.commandPool = _command_pool;
		allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocate_info.commandBufferCount = frame_slot_count;
		if (_vk.AllocateCommandBuffers(_device, &allocate_info, command_buffers.data()) != VK_SUCCESS)
			return false;

		const VkFenceCreateInfo fence_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
		for (size_t index = 0; index < _frame_slots.size(); ++index)
		{
			_frame_slots[index].commands = command_buffers[index];
			if (_vk.CreateFence(_device, &fence_info, nullptr, &_frame_slots[index].fence) != VK_SUCCESS)
				return false;
		}
		return true;
	}

	bool vulkan_quad_device::create_render_pass(VkFormat format)
	{
		// OpenXR hands over an acquired color image in this layout and expects it back in the same one.
		VkAttachmentDescription attachment = {};
		attachment.format = format;
		attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		const VkAttachmentReference color_reference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &color_reference;
		VkSubpassDependency dependencies[2] = {};
		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		VkRenderPassCreateInfo render_pass_info = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
		render_pass_info.attachmentCount = 1;
		render_pass_info.pAttachments = &attachment;
		render_pass_info.subpassCount = 1;
		render_pass_info.pSubpasses = &subpass;
		render_pass_info.dependencyCount = 2;
		render_pass_info.pDependencies = dependencies;
		return _render_pass == VK_NULL_HANDLE && _vk.CreateRenderPass(_device, &render_pass_info, nullptr, &_render_pass) == VK_SUCCESS;
	}

	vulkan_renderer_setup vulkan_quad_device::renderer_setup(uint32_t image_count) const
	{
		vulkan_renderer_setup setup;
		setup.instance = _instance;
		setup.physical_device = _physical_device;
		setup.device = _device;
		setup.queue_family = _queue_family;
		setup.queue = _queue;
		setup.render_pass = _render_pass;
		setup.frame_count = (std::max)(image_count, frame_slot_count);
		setup.get_instance_proc_addr = _get_instance_proc_addr;
		return setup;
	}

	bool vulkan_quad_device::create_images(const std::vector<VkImage> &images, VkFormat format, uint32_t width, uint32_t height, vulkan_quad_images &target)
	{
		if (_render_pass == VK_NULL_HANDLE)
			return false;
		target.width = width;
		target.height = height;
		for (const VkImage image : images)
		{
			VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
			view_info.image = image;
			view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
			view_info.format = format;
			view_info.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			VkImageView view = VK_NULL_HANDLE;
			if (image == VK_NULL_HANDLE || _vk.CreateImageView(_device, &view_info, nullptr, &view) != VK_SUCCESS)
				return false;
			target.views.push_back(view);

			VkFramebufferCreateInfo framebuffer_info = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
			framebuffer_info.renderPass = _render_pass;
			framebuffer_info.attachmentCount = 1;
			framebuffer_info.pAttachments = &view;
			framebuffer_info.width = width;
			framebuffer_info.height = height;
			framebuffer_info.layers = 1;
			VkFramebuffer framebuffer = VK_NULL_HANDLE;
			if (_vk.CreateFramebuffer(_device, &framebuffer_info, nullptr, &framebuffer) != VK_SUCCESS)
				return false;
			target.framebuffers.push_back(framebuffer);
		}
		return true;
	}

	void vulkan_quad_device::destroy_images(vulkan_quad_images &target)
	{
		for (const VkFramebuffer framebuffer : target.framebuffers)
			_vk.DestroyFramebuffer(_device, framebuffer, nullptr);
		for (const VkImageView view : target.views)
			_vk.DestroyImageView(_device, view, nullptr);
		target.framebuffers.clear();
		target.views.clear();
	}

	bool vulkan_quad_device::slot_free()
	{
		frame_slot &slot = _frame_slots[_next_frame_slot];
		if (slot.submitted && _vk.GetFenceStatus(_device, slot.fence) == VK_SUCCESS)
			slot.submitted = false;
		return !slot.submitted;
	}

	VkCommandBuffer vulkan_quad_device::begin_commands(const vulkan_quad_images &target, uint32_t image_index)
	{
		if (image_index >= target.framebuffers.size() || !slot_free())
			return VK_NULL_HANDLE;
		const frame_slot &slot = _frame_slots[_next_frame_slot];
		VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		if (_vk.ResetCommandBuffer(slot.commands, 0) != VK_SUCCESS || _vk.BeginCommandBuffer(slot.commands, &begin_info) != VK_SUCCESS)
			return VK_NULL_HANDLE;

		const VkClearValue transparent = {};
		VkRenderPassBeginInfo render_pass_info = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
		render_pass_info.renderPass = _render_pass;
		render_pass_info.framebuffer = target.framebuffers[image_index];
		render_pass_info.renderArea = { { 0, 0 }, { target.width, target.height } };
		render_pass_info.clearValueCount = 1;
		render_pass_info.pClearValues = &transparent;
		_vk.CmdBeginRenderPass(slot.commands, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
		return slot.commands;
	}

	bool vulkan_quad_device::submit_commands()
	{
		frame_slot &slot = _frame_slots[_next_frame_slot];
		_vk.CmdEndRenderPass(slot.commands);
		if (_vk.EndCommandBuffer(slot.commands) != VK_SUCCESS || _vk.ResetFences(_device, 1, &slot.fence) != VK_SUCCESS)
			return false;
		VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &slot.commands;
		if (_vk.QueueSubmit(_queue, 1, &submit_info, slot.fence) != VK_SUCCESS)
			return false;
		slot.submitted = true;
		_next_frame_slot = (_next_frame_slot + 1) % _frame_slots.size();
		return true;
	}

	void vulkan_quad_device::wait_for_submitted_commands()
	{
		std::array<VkFence, frame_slot_count> fences = {};
		uint32_t fence_count = 0;
		for (const frame_slot &slot : _frame_slots)
			if (slot.submitted)
				fences[fence_count++] = slot.fence;
		if (fence_count != 0)
			_vk.WaitForFences(_device, fence_count, fences.data(), VK_TRUE, session_end_wait_nanoseconds);
		for (frame_slot &slot : _frame_slots)
			slot.submitted = false;
	}
}
