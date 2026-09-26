#pragma once

#include <Windows.h>
#include <wrl/client.h>

#include <cstdint>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D12Device;
struct ID3D12CommandQueue;
struct IDXGISwapChain1;

namespace reshade::api
{
	struct effect_runtime;
}

namespace openxr_ui
{
	// One eye view of a projection layer: a swapchain texture (ID3D11Texture2D or ID3D12Resource) and its region.
	struct effect_view
	{
		void *texture = nullptr;
		uint32_t array_index = 0;
		int32_t x = 0;
		int32_t y = 0;
		uint32_t width = 0;
		uint32_t height = 0;
	};

	// Effect runtime for sessions that official ReShade skipped because their device is not wrapped by ReShade.
	class effect_fallback
	{
	public:
		effect_fallback(ID3D11Device *device, ID3D11DeviceContext *context);
		effect_fallback(ID3D12Device *device, ID3D12CommandQueue *queue);
		~effect_fallback();

		effect_fallback(const effect_fallback &) = delete;
		effect_fallback &operator=(const effect_fallback &) = delete;

		// Applies the effects in place to the frame's eye views, just before their images are released.
		// A view without a texture is skipped but keeps its place, so the effect picture keeps its size.
		void apply(const effect_view *views, uint32_t view_count);

	private:
		bool prepare_runtime(uint32_t width, uint32_t height, uint32_t source_format);
		void destroy_runtime();

		bool _d3d12 = false;
		Microsoft::WRL::ComPtr<IUnknown> _device;
		Microsoft::WRL::ComPtr<IUnknown> _queue;
		Microsoft::WRL::ComPtr<IDXGISwapChain1> _swapchain;
		reshade::api::effect_runtime *_runtime = nullptr;
		uint64_t _rtv = 0;
		uint64_t _rtv_srgb = 0;
		uint32_t _width = 0;
		uint32_t _height = 0;
		uint32_t _format = 0;
		bool _failed = false;
	};
}
