// Draws the headset menu panel into PNG files without a game, a simulator window or OpenXR, for fast design work.
// The images are the panel texture itself, drawn by the same renderer the add-on uses, in an sRGB target like the
// headset swapchain. Final visual acceptance still uses capture_simulator_ui.ps1.

#include "openxr_ui_renderer.hpp"

#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
	using Microsoft::WRL::ComPtr;

	openxr_ui::technique make_technique(uint64_t id, const char *name, const char *effect, bool enabled, bool changed = false,
		bool favourite = false)
	{
		openxr_ui::technique entry;
		entry.id = id;
		entry.name = name;
		entry.effect_name = effect;
		entry.enabled = enabled;
		entry.has_non_default_values = changed;
		entry.favourite = favourite;
		return entry;
	}

	openxr_ui::parameter make_parameter(uint64_t id, const char *name, const char *effect, openxr_ui::parameter_type type,
		std::vector<double> values, std::vector<double> defaults, double minimum, double maximum, double step)
	{
		openxr_ui::parameter entry;
		entry.id = id;
		entry.name = name;
		entry.effect_name = effect;
		entry.type = type;
		entry.values = std::move(values);
		entry.default_values = std::move(defaults);
		entry.minimum = minimum;
		entry.maximum = maximum;
		entry.step = step;
		return entry;
	}

	openxr_ui::technique_snapshot sample_snapshot()
	{
		using openxr_ui::parameter_type;
		openxr_ui::technique_snapshot snapshot;
		snapshot.techniques = {
			make_technique(1, "LumaSharpen", "LumaSharpen.fx", true, true, true),
			make_technique(2, "Vibrance", "Vibrance.fx", true),
			make_technique(3, "MXAO", "qUINT_mxao.fx", false),
			make_technique(4, "AdaptiveSharpen", "AdaptiveSharpen.fx", false),
			make_technique(5, "Clarity", "Clarity.fx", false, false, true),
			make_technique(6, "Tonemap", "Tonemap.fx", false),
			make_technique(7, "CinematicDepthOfFieldWithAVeryLongTechniqueName", "CinematicDOF.fx", false),
			make_technique(8, "SMAA", "SMAA.fx", true),
			make_technique(9, "Deband", "Deband.fx", false),
			make_technique(10, "Levels", "Levels.fx", false),
		};
		snapshot.techniques.front().tooltip = "Sharpens the image using the brightness of neighbouring pixels.";
		snapshot.parameters = {
			make_parameter(100, "Sharpening strength", "LumaSharpen.fx", parameter_type::floating_point, { 0.85 }, { 0.65 }, 0.1, 3.0, 0.01),
			make_parameter(101, "Sharpening limit", "LumaSharpen.fx", parameter_type::floating_point, { 0.035 }, { 0.035 }, 0.0, 1.0, 0.005),
			make_parameter(102, "Sample pattern", "LumaSharpen.fx", parameter_type::signed_integer, { 2 }, { 2 }, 0.0, 3.0, 1.0),
			make_parameter(103, "Show sharpening amount as a debug overlay", "LumaSharpen.fx", parameter_type::boolean, { 0.0 }, { 0.0 }, 0.0, 1.0, 1.0),
			make_parameter(104, "Offset bias", "LumaSharpen.fx", parameter_type::floating_point, { 1.0 }, { 1.0 }, 0.0, 6.0, 0.1),
			make_parameter(105, "Tint", "LumaSharpen.fx", parameter_type::floating_point, { 1.0, 0.92, 0.8 }, { 1.0, 1.0, 1.0 }, 0.0, 1.0, 0.01),
		};
		openxr_ui::update_technique_default_states(snapshot);
		return snapshot;
	}

	bool save_png(ID3D11Device *device, ID3D11DeviceContext *context, ID3D11Texture2D *texture, uint32_t height, const std::wstring &path)
	{
		D3D11_TEXTURE2D_DESC desc = {};
		texture->GetDesc(&desc);
		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		ComPtr<ID3D11Texture2D> staging;
		if (FAILED(device->CreateTexture2D(&desc, nullptr, &staging)))
			return false;
		context->CopyResource(staging.Get(), texture);
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
			return false;

		// The panel is drawn on a transparent clear, so show it over a mid grey like a game scene behind it.
		std::vector<uint8_t> pixels(static_cast<size_t>(desc.Width) * height * 4);
		for (uint32_t y = 0; y < height; ++y)
			for (uint32_t x = 0; x < desc.Width; ++x)
			{
				const uint8_t *source = static_cast<const uint8_t *>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch + x * 4;
				uint8_t *target = pixels.data() + (static_cast<size_t>(y) * desc.Width + x) * 4;
				const uint32_t alpha = source[3];
				for (int channel = 0; channel < 3; ++channel)
					target[2 - channel] = static_cast<uint8_t>((source[channel] * alpha + 96 * (255 - alpha)) / 255);
				target[3] = 255;
			}
		context->Unmap(staging.Get(), 0);

		ComPtr<IWICImagingFactory> factory;
		ComPtr<IWICStream> stream;
		ComPtr<IWICBitmapEncoder> encoder;
		ComPtr<IWICBitmapFrameEncode> frame;
		WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
		return SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
			SUCCEEDED(factory->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
			SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
			SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
			SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
			SUCCEEDED(frame->SetSize(desc.Width, height)) && SUCCEEDED(frame->SetPixelFormat(&format)) &&
			SUCCEEDED(frame->WritePixels(height, desc.Width * 4, static_cast<UINT>(pixels.size()), pixels.data())) &&
			SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
	}

	std::wstring file_name(const char *theme)
	{
		std::wstring result;
		for (const char *character = theme; *character != '\0'; ++character)
			result.push_back(*character == ' ' ? L'-' : static_cast<wchar_t>(std::tolower(static_cast<unsigned char>(*character))));
		return result;
	}
}

int wmain(int argc, wchar_t **argv)
{
	if (argc < 2)
	{
		std::fputs("Usage: ReShade-OpenXR-UI-Preview.exe <output folder>\n", stderr);
		return 2;
	}
	const std::wstring output = argv[1];
	if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
		return 1;

	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)))
	{
		std::fputs("Could not create the software Direct3D 11 device.\n", stderr);
		return 1;
	}

	D3D11_TEXTURE2D_DESC texture_desc = {};
	texture_desc.Width = openxr_ui::panel_width;
	texture_desc.Height = openxr_ui::expanded_panel_height;
	texture_desc.MipLevels = 1;
	texture_desc.ArraySize = 1;
	texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	texture_desc.SampleDesc.Count = 1;
	texture_desc.Usage = D3D11_USAGE_DEFAULT;
	texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
	ComPtr<ID3D11Texture2D> texture;
	ComPtr<ID3D11RenderTargetView> render_target;
	if (FAILED(device->CreateTexture2D(&texture_desc, nullptr, &texture)) ||
		FAILED(device->CreateRenderTargetView(texture.Get(), nullptr, &render_target)))
		return 1;

	openxr_ui::panel_renderer renderer;
	if (!renderer.initialize(device.Get(), context.Get(), texture_desc.Width, texture_desc.Height))
		return 1;
	renderer.set_srgb_target(true);

	const openxr_ui::technique_snapshot snapshot = sample_snapshot();
	struct shot
	{
		const char *name;
		openxr_ui::panel_renderer::page page;
		bool expanded;
		bool dirty;
		bool uevr = false;
	};
	const shot shots[] = {
		{ "effects", openxr_ui::panel_renderer::page::effects, false, true },
		{ "effects-uevr", openxr_ui::panel_renderer::page::effects, false, false, true },
		{ "detail", openxr_ui::panel_renderer::page::effect_detail, false, true },
		{ "save-as", openxr_ui::panel_renderer::page::save_as, false, false },
		{ "effects-expanded", openxr_ui::panel_renderer::page::effects, true, false },
		{ "detail-expanded", openxr_ui::panel_renderer::page::effect_detail, true, false },
	};

	int written = 0;
	for (uint32_t theme = 0; theme < vr_theme::theme_count; ++theme)
		for (const shot &entry : shots)
		{
			// Only the default theme gets every page; the others show the two main pages to compare colors.
			if (theme != vr_theme::default_theme && (entry.page == openxr_ui::panel_renderer::page::save_as || entry.uevr))
				continue;
			openxr_ui::render_input input;
			input.theme = theme;
			input.expanded = entry.expanded;
			input.preset_dirty = entry.dirty;
			input.depth_effects_unavailable = entry.uevr;
			input.preset_name = "Sharp and Vivid";
			// A high-resolution headset gives the longest header text, so clipped labels show up.
			input.eye_width = 4320;
			input.eye_height = 4320;
			renderer.show_page(entry.page, 1, "Sharp and Vivid Copy");
			// The first frame after a change only lays out; the second draws the settled result.
			renderer.render(render_target.Get(), snapshot, input);
			renderer.render(render_target.Get(), snapshot, input);
			const uint32_t height = entry.expanded ? openxr_ui::expanded_panel_height : openxr_ui::compact_panel_height;
			const std::wstring path = output + L"\\" + file_name(vr_theme::theme_names[theme]) + L"-" +
				std::wstring(entry.name, entry.name + std::strlen(entry.name)) + L".png";
			if (!save_png(device.Get(), context.Get(), texture.Get(), height, path))
			{
				std::fwprintf(stderr, L"Could not write %ls\n", path.c_str());
				return 1;
			}
			++written;
		}
	std::printf("Wrote %d panel previews.\n", written);
	return 0;
}
