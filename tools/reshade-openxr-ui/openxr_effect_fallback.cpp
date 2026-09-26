#include <reshade.hpp>

#include "openxr_effect_fallback.hpp"
#include "vr_ui_state.hpp"

#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <algorithm>
#include <cstdio>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace reshade::api;

namespace
{
	// Relative config paths resolve against ReShade's base folder, next to the official VR configuration.
	constexpr const char *config_path = "ReShadeVR.ini";

	void log_failure(const char *reason, HRESULT result = S_OK)
	{
		char message[256];
		std::snprintf(message, sizeof(message), "effect fallback unavailable: %s (0x%08lX).", reason, static_cast<unsigned long>(result));
		openxr_ui_log_message(message, true);
	}

	// Flip-model swapchains accept only these formats. Each shares a format family with the headset image, so copies between them are allowed.
	DXGI_FORMAT swapchain_format(DXGI_FORMAT source)
	{
		switch (source)
		{
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
			return DXGI_FORMAT_B8G8R8A8_UNORM;
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
			return DXGI_FORMAT_R8G8B8A8_UNORM;
		case DXGI_FORMAT_R16G16B16A16_TYPELESS:
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
			return DXGI_FORMAT_R16G16B16A16_FLOAT;
		case DXGI_FORMAT_R10G10B10A2_TYPELESS:
		case DXGI_FORMAT_R10G10B10A2_UNORM:
			return DXGI_FORMAT_R10G10B10A2_UNORM;
		default:
			return DXGI_FORMAT_UNKNOWN;
		}
	}

	bool describe_texture(bool d3d12, void *texture, DXGI_FORMAT &format, UINT &samples)
	{
		if (d3d12)
		{
			const D3D12_RESOURCE_DESC desc = static_cast<ID3D12Resource *>(texture)->GetDesc();
			format = desc.Format;
			samples = desc.SampleDesc.Count;
			return desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		}
		D3D11_TEXTURE2D_DESC desc = {};
		static_cast<ID3D11Texture2D *>(texture)->GetDesc(&desc);
		format = desc.Format;
		samples = desc.SampleDesc.Count;
		return true;
	}

	resource to_resource(void *texture)
	{
		return { reinterpret_cast<uintptr_t>(texture) };
	}
}

openxr_ui::effect_fallback::effect_fallback(ID3D11Device *device, ID3D11DeviceContext *context)
{
	// ReShade compares the swapchain's device with the device pointer, so both must be the same interface.
	device->QueryInterface(IID_PPV_ARGS(reinterpret_cast<ID3D11Device **>(_device.ReleaseAndGetAddressOf())));
	_queue = context;
}

openxr_ui::effect_fallback::effect_fallback(ID3D12Device *device, ID3D12CommandQueue *queue) :
	_d3d12(true)
{
	device->QueryInterface(IID_PPV_ARGS(reinterpret_cast<ID3D12Device **>(_device.ReleaseAndGetAddressOf())));
	_queue = queue;
}

openxr_ui::effect_fallback::~effect_fallback()
{
	destroy_runtime();
}

bool openxr_ui::effect_fallback::prepare_runtime(uint32_t width, uint32_t height, uint32_t source_format)
{
	const DXGI_FORMAT target_format = swapchain_format(static_cast<DXGI_FORMAT>(source_format));
	if (_runtime != nullptr && width == _width && height == _height && target_format == static_cast<DXGI_FORMAT>(_format))
		return true;
	if (_failed || _device == nullptr)
		return false;
	destroy_runtime();
	// Every failure below is permanent for this session, so it is reported once instead of every frame.
	_failed = true;
	if (target_format == DXGI_FORMAT_UNKNOWN)
	{
		log_failure("the headset image format has no matching swapchain format", static_cast<HRESULT>(source_format));
		return false;
	}

	ComPtr<IDXGIFactory2> factory;
	HRESULT result = S_OK;
	if (_d3d12)
	{
		result = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
	}
	else
	{
		ComPtr<IDXGIDevice> dxgi_device;
		ComPtr<IDXGIAdapter> adapter;
		result = _device.As(&dxgi_device);
		if (SUCCEEDED(result))
			result = dxgi_device->GetAdapter(&adapter);
		if (SUCCEEDED(result))
			result = adapter->GetParent(IID_PPV_ARGS(&factory));
	}
	if (FAILED(result))
	{
		log_failure("no DXGI factory for the application device", result);
		return false;
	}

	// This windowless swapchain is never presented. It only gives ReShade a back buffer the size of the headset image,
	// which the eye views are copied into and out of.
	DXGI_SWAP_CHAIN_DESC1 desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.Format = target_format;
	desc.SampleDesc.Count = 1;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.BufferCount = 2;
	desc.Scaling = DXGI_SCALING_STRETCH;
	desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
	result = factory->CreateSwapChainForComposition(_d3d12 ? _queue.Get() : _device.Get(), &desc, nullptr, &_swapchain);
	if (FAILED(result))
	{
		log_failure("could not create the hidden swapchain", result);
		return false;
	}

	if (!reshade::create_effect_runtime(_d3d12 ? device_api::d3d12 : device_api::d3d11, _device.Get(), _queue.Get(), _swapchain.Get(), config_path, &_runtime) ||
		_runtime == nullptr)
	{
		_runtime = nullptr;
		_swapchain.Reset();
		log_failure("ReShade could not create an effect runtime for the application device (the installed ReShade needs add-on support)");
		return false;
	}

	device *const graphics_device = _runtime->get_device();
	const resource back_buffer = _runtime->get_current_back_buffer();
	const format back_buffer_format = graphics_device->get_resource_desc(back_buffer).texture.format;
	resource_view rtv = {};
	resource_view rtv_srgb = {};
	if (!graphics_device->create_resource_view(back_buffer, resource_usage::render_target, resource_view_desc(format_to_default_typed(back_buffer_format, 0)), &rtv) ||
		!graphics_device->create_resource_view(back_buffer, resource_usage::render_target, resource_view_desc(format_to_default_typed(back_buffer_format, 1)), &rtv_srgb))
	{
		graphics_device->destroy_resource_view(rtv);
		destroy_runtime();
		log_failure("could not create render target views for the hidden swapchain");
		return false;
	}
	_rtv = rtv.handle;
	_rtv_srgb = rtv_srgb.handle;
	_width = width;
	_height = height;
	_format = static_cast<uint32_t>(target_format);
	_failed = false;

	char message[192];
	std::snprintf(message, sizeof(message), "effect fallback started a ReShade effect runtime for %ux%u headset images on the application's %s device.",
		width, height, _d3d12 ? "Direct3D 12" : "Direct3D 11");
	openxr_ui_log_message(message);
	return true;
}

void openxr_ui::effect_fallback::destroy_runtime()
{
	if (_runtime != nullptr)
	{
		_runtime->get_command_queue()->wait_idle();
		device *const graphics_device = _runtime->get_device();
		graphics_device->destroy_resource_view({ _rtv_srgb });
		graphics_device->destroy_resource_view({ _rtv });
		reshade::destroy_effect_runtime(_runtime);
		_runtime = nullptr;
	}
	_swapchain.Reset();
	_rtv = _rtv_srgb = 0;
	_width = _height = _format = 0;
}

void openxr_ui::effect_fallback::apply(const effect_view *views, uint32_t view_count)
{
	uint32_t width = 0;
	uint32_t height = 0;
	void *first_texture = nullptr;
	for (uint32_t index = 0; index < view_count; ++index)
	{
		if (views[index].x < 0 || views[index].y < 0)
			return;
		width += views[index].width;
		height = (std::max)(height, views[index].height);
		if (first_texture == nullptr)
			first_texture = views[index].texture;
	}
	DXGI_FORMAT source_format = DXGI_FORMAT_UNKNOWN;
	UINT samples = 0;
	if (first_texture == nullptr || width == 0 || height == 0 || !describe_texture(_d3d12, first_texture, source_format, samples) || samples > 1 ||
		!prepare_runtime(width, height, static_cast<uint32_t>(source_format)))
		return;

	// Several views often share one double-width image; each image may only be transitioned once.
	std::vector<resource> images;
	for (uint32_t index = 0; index < view_count; ++index)
		if (views[index].texture != nullptr && std::find(images.begin(), images.end(), to_resource(views[index].texture)) == images.end())
			images.push_back(to_resource(views[index].texture));
	const uint32_t image_count = static_cast<uint32_t>(images.size());
	// OpenXR hands Direct3D 12 color images back in the render target state; Direct3D 11 ignores these barriers.
	const std::vector<resource_usage> released_states(image_count, resource_usage::render_target);
	const std::vector<resource_usage> copy_source_states(image_count, resource_usage::copy_source);
	const std::vector<resource_usage> copy_dest_states(image_count, resource_usage::copy_dest);
	const auto image_box = [views](uint32_t index) {
		const effect_view &view = views[index];
		return subresource_box { static_cast<uint32_t>(view.x), static_cast<uint32_t>(view.y), 0,
			static_cast<uint32_t>(view.x) + view.width, static_cast<uint32_t>(view.y) + view.height, 1 };
	};
	const auto image_subresource = [this, views](uint32_t index) {
		return views[index].array_index * _runtime->get_device()->get_resource_desc(to_resource(views[index].texture)).texture.levels;
	};

	command_list *const commands = _runtime->get_command_queue()->get_immediate_command_list();
	const resource back_buffer = _runtime->get_current_back_buffer();

	commands->barrier(image_count, images.data(), released_states.data(), copy_source_states.data());
	commands->barrier(back_buffer, resource_usage::present, resource_usage::copy_dest);
	uint32_t offset = 0;
	for (uint32_t index = 0; index < view_count; ++index)
	{
		const subresource_box source_box = image_box(index);
		const subresource_box target_box = { offset, 0, 0, offset + views[index].width, views[index].height, 1 };
		if (views[index].texture != nullptr)
			commands->copy_texture_region(to_resource(views[index].texture), image_subresource(index), &source_box, back_buffer, 0, &target_box);
		offset += views[index].width;
	}
	commands->barrier(back_buffer, resource_usage::copy_dest, resource_usage::render_target);

	// The back buffer counts as the present target, so ReShade uses its default effect setup for it.
	_runtime->render_effects(commands, { _rtv }, { _rtv_srgb });

	commands->barrier(back_buffer, resource_usage::render_target, resource_usage::copy_source);
	commands->barrier(image_count, images.data(), copy_source_states.data(), copy_dest_states.data());
	offset = 0;
	for (uint32_t index = 0; index < view_count; ++index)
	{
		const subresource_box source_box = { offset, 0, 0, offset + views[index].width, views[index].height, 1 };
		const subresource_box target_box = image_box(index);
		if (views[index].texture != nullptr)
			commands->copy_texture_region(back_buffer, 0, &source_box, to_resource(views[index].texture), image_subresource(index), &target_box);
		offset += views[index].width;
	}
	commands->barrier(image_count, images.data(), copy_dest_states.data(), released_states.data());
	commands->barrier(back_buffer, resource_usage::copy_source, resource_usage::present);

	// Loads effects, advances timers and submits everything recorded above. Effects were already rendered this frame,
	// so ReShade does not render them again, and its overlay lands on the back buffer after the eyes were copied out.
	reshade::update_and_present_effect_runtime(_runtime);
}
