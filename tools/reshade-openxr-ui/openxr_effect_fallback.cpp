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

	// Typeless twin of a swapchain format, so ReShade can create both linear and sRGB views of the back buffer.
	DXGI_FORMAT typeless_format(DXGI_FORMAT format)
	{
		switch (format)
		{
		case DXGI_FORMAT_B8G8R8A8_UNORM:
			return DXGI_FORMAT_B8G8R8A8_TYPELESS;
		case DXGI_FORMAT_R8G8B8A8_UNORM:
			return DXGI_FORMAT_R8G8B8A8_TYPELESS;
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
			return DXGI_FORMAT_R16G16B16A16_TYPELESS;
		case DXGI_FORMAT_R10G10B10A2_UNORM:
			return DXGI_FORMAT_R10G10B10A2_TYPELESS;
		default:
			return format;
		}
	}

	// ReShade's effect runtime needs a swapchain object, but only reads its device, description and back buffer.
	// A real DXGI swapchain, even one never presented, is seen by every layer that hooks swapchain creation on the
	// application's queue (for example frame generation), and a second swapchain there can stop the game's frames.
	// This stand-in is a plain texture on the application device that no other software ever sees.
	class offscreen_swapchain final : public IDXGISwapChain3
	{
	public:
		offscreen_swapchain(IUnknown *device, IUnknown *back_buffer, UINT width, UINT height, DXGI_FORMAT format) :
			_device(device), _back_buffer(back_buffer), _width(width), _height(height), _format(format)
		{
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override
		{
			if (object == nullptr)
				return E_POINTER;
			if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIDeviceSubObject) ||
				riid == __uuidof(IDXGISwapChain) || riid == __uuidof(IDXGISwapChain1) || riid == __uuidof(IDXGISwapChain2) || riid == __uuidof(IDXGISwapChain3))
			{
				AddRef();
				*object = static_cast<IDXGISwapChain3 *>(this);
				return S_OK;
			}
			*object = nullptr;
			return E_NOINTERFACE;
		}
		ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&_references); }
		ULONG STDMETHODCALLTYPE Release() override
		{
			const ULONG references = InterlockedDecrement(&_references);
			if (references == 0)
				delete this;
			return references;
		}

		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID name, UINT size, const void *data) override
		{
			private_entry *const entry = find_private(name, true);
			entry->object.Reset();
			entry->bytes.assign(static_cast<const BYTE *>(data), static_cast<const BYTE *>(data) + (data != nullptr ? size : 0));
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID name, const IUnknown *object) override
		{
			private_entry *const entry = find_private(name, true);
			entry->bytes.clear();
			entry->object = const_cast<IUnknown *>(object);
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID name, UINT *size, void *data) override
		{
			if (size == nullptr)
				return E_INVALIDARG;
			const private_entry *const entry = find_private(name, false);
			if (entry == nullptr || (entry->object == nullptr && entry->bytes.empty()))
			{
				*size = 0;
				return DXGI_ERROR_NOT_FOUND;
			}
			const UINT stored_size = entry->object != nullptr ? static_cast<UINT>(sizeof(IUnknown *)) : static_cast<UINT>(entry->bytes.size());
			if (data == nullptr)
			{
				*size = stored_size;
				return S_OK;
			}
			if (*size < stored_size)
			{
				*size = stored_size;
				return DXGI_ERROR_MORE_DATA;
			}
			*size = stored_size;
			if (entry->object != nullptr)
			{
				entry->object->AddRef();
				*static_cast<IUnknown **>(data) = entry->object.Get();
			}
			else
			{
				std::copy(entry->bytes.begin(), entry->bytes.end(), static_cast<BYTE *>(data));
			}
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE GetParent(REFIID, void **parent) override { return not_available(parent); }
		HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void **device) override { return _device->QueryInterface(riid, device); }

		HRESULT STDMETHODCALLTYPE Present(UINT, UINT) override { return S_OK; }
		HRESULT STDMETHODCALLTYPE GetBuffer(UINT index, REFIID riid, void **surface) override
		{
			return index == 0 ? _back_buffer->QueryInterface(riid, surface) : not_available(surface, DXGI_ERROR_INVALID_CALL);
		}
		HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL, IDXGIOutput *) override { return DXGI_ERROR_INVALID_CALL; }
		HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL *fullscreen, IDXGIOutput **target) override
		{
			if (fullscreen != nullptr)
				*fullscreen = FALSE;
			if (target != nullptr)
				*target = nullptr;
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC *desc) override
		{
			if (desc == nullptr)
				return E_INVALIDARG;
			*desc = {};
			desc->BufferDesc.Width = _width;
			desc->BufferDesc.Height = _height;
			desc->BufferDesc.Format = _format;
			desc->SampleDesc.Count = 1;
			desc->BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			desc->BufferCount = 1;
			desc->Windowed = TRUE;
			desc->SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT, UINT, UINT, DXGI_FORMAT, UINT) override { return DXGI_ERROR_INVALID_CALL; }
		HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC *) override { return DXGI_ERROR_INVALID_CALL; }
		HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput **output) override { return not_available(output, DXGI_ERROR_UNSUPPORTED); }
		HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS *) override { return DXGI_ERROR_FRAME_STATISTICS_DISJOINT; }
		HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT *count) override
		{
			if (count == nullptr)
				return E_INVALIDARG;
			*count = 0;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1 *desc) override
		{
			if (desc == nullptr)
				return E_INVALIDARG;
			*desc = {};
			desc->Width = _width;
			desc->Height = _height;
			desc->Format = _format;
			desc->SampleDesc.Count = 1;
			desc->BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			desc->BufferCount = 1;
			desc->SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC *) override { return DXGI_ERROR_INVALID_CALL; }
		HRESULT STDMETHODCALLTYPE GetHwnd(HWND *hwnd) override
		{
			if (hwnd != nullptr)
				*hwnd = nullptr;
			return DXGI_ERROR_INVALID_CALL;
		}
		HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID, void **window) override { return not_available(window, DXGI_ERROR_INVALID_CALL); }
		HRESULT STDMETHODCALLTYPE Present1(UINT, UINT, const DXGI_PRESENT_PARAMETERS *) override { return S_OK; }
		BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported() override { return FALSE; }
		HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput **output) override { return not_available(output, S_OK); }
		HRESULT STDMETHODCALLTYPE SetBackgroundColor(const DXGI_RGBA *) override { return DXGI_ERROR_INVALID_CALL; }
		HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA *) override { return DXGI_ERROR_INVALID_CALL; }
		HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION) override { return DXGI_ERROR_INVALID_CALL; }
		HRESULT STDMETHODCALLTYPE GetRotation(DXGI_MODE_ROTATION *) override { return DXGI_ERROR_INVALID_CALL; }

		HRESULT STDMETHODCALLTYPE SetSourceSize(UINT, UINT) override { return DXGI_ERROR_INVALID_CALL; }
		HRESULT STDMETHODCALLTYPE GetSourceSize(UINT *width, UINT *height) override
		{
			if (width != nullptr)
				*width = _width;
			if (height != nullptr)
				*height = _height;
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT) override { return DXGI_ERROR_INVALID_CALL; }
		HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT *) override { return DXGI_ERROR_INVALID_CALL; }
		HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject() override { return nullptr; }
		HRESULT STDMETHODCALLTYPE SetMatrixTransform(const DXGI_MATRIX_3X2_F *) override { return DXGI_ERROR_INVALID_CALL; }
		HRESULT STDMETHODCALLTYPE GetMatrixTransform(DXGI_MATRIX_3X2_F *) override { return DXGI_ERROR_INVALID_CALL; }

		UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex() override { return 0; }
		HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE color_space, UINT *support) override
		{
			if (support == nullptr)
				return E_INVALIDARG;
			const bool supported = color_space == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 ||
				(color_space == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 && _format == DXGI_FORMAT_R16G16B16A16_FLOAT);
			*support = supported ? DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT : 0;
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE) override { return DXGI_ERROR_INVALID_CALL; }
		HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT, UINT, UINT, DXGI_FORMAT, UINT, const UINT *, IUnknown *const *) override { return DXGI_ERROR_INVALID_CALL; }

	private:
		struct private_entry
		{
			GUID name;
			std::vector<BYTE> bytes;
			ComPtr<IUnknown> object;
		};

		~offscreen_swapchain() = default;

		template <typename T>
		static HRESULT not_available(T **object, HRESULT result = E_NOINTERFACE)
		{
			if (object != nullptr)
				*object = nullptr;
			return result;
		}

		private_entry *find_private(REFGUID name, bool create)
		{
			for (private_entry &entry : _private_data)
				if (entry.name == name)
					return &entry;
			if (!create)
				return nullptr;
			_private_data.push_back({ name });
			return &_private_data.back();
		}

		volatile ULONG _references = 1;
		ComPtr<IUnknown> _device;
		ComPtr<IUnknown> _back_buffer;
		UINT _width;
		UINT _height;
		DXGI_FORMAT _format;
		std::vector<private_entry> _private_data;
	};

	HRESULT create_back_buffer(bool d3d12, IUnknown *device, UINT width, UINT height, DXGI_FORMAT format, ComPtr<IUnknown> &back_buffer)
	{
		if (d3d12)
		{
			D3D12_HEAP_PROPERTIES heap = {};
			heap.Type = D3D12_HEAP_TYPE_DEFAULT;
			D3D12_RESOURCE_DESC desc = {};
			desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			desc.Width = width;
			desc.Height = height;
			desc.DepthOrArraySize = 1;
			desc.MipLevels = 1;
			desc.Format = format;
			desc.SampleDesc.Count = 1;
			desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
			ComPtr<ID3D12Resource> texture;
			// The common state equals the present state that the copy barriers start from.
			const HRESULT result = static_cast<ID3D12Device *>(device)->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&texture));
			back_buffer = texture;
			return result;
		}
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = format;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		ComPtr<ID3D11Texture2D> texture;
		const HRESULT result = static_cast<ID3D11Device *>(device)->CreateTexture2D(&desc, nullptr, &texture);
		back_buffer = texture;
		return result;
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

	// The back buffer is the size of the headset image; the eye views are copied into and out of it.
	ComPtr<IUnknown> back_buffer_texture;
	const HRESULT result = create_back_buffer(_d3d12, _device.Get(), width, height, typeless_format(target_format), back_buffer_texture);
	if (FAILED(result))
	{
		log_failure("could not create the effect back buffer", result);
		return false;
	}
	_swapchain.Attach(new offscreen_swapchain(_device.Get(), back_buffer_texture.Get(), width, height, target_format));

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
		log_failure("could not create render target views for the effect back buffer");
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
