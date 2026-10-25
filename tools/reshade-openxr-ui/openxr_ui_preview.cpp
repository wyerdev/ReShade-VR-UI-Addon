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
		uint32_t menu_button = openxr_ui::menu_button_hold_both_sticks;
		bool all_themes = true;
	};
	const shot shots[] = {
		{ "effects", openxr_ui::panel_renderer::page::effects, false, true },
		{ "effects-uevr", openxr_ui::panel_renderer::page::effects, false, false, true },
		{ "detail", openxr_ui::panel_renderer::page::effect_detail, false, true },
		{ "save-as", openxr_ui::panel_renderer::page::save_as, false, false },
		{ "effects-expanded", openxr_ui::panel_renderer::page::effects, true, false },
		{ "detail-expanded", openxr_ui::panel_renderer::page::effect_detail, true, false },
		{ "settings", openxr_ui::panel_renderer::page::settings, false, false },
		// The L3+R3 gesture is unavailable while L3+R3 opens the menu; only the default theme needs this.
		{ "settings-both-sticks", openxr_ui::panel_renderer::page::settings, false, false, false, openxr_ui::menu_button_both_sticks, false },
	};

	int written = 0;
	for (uint32_t theme = 0; theme < vr_theme::theme_count; ++theme)
		for (const shot &entry : shots)
		{
			// Only the default theme gets every page; the others show the two main pages to compare colors.
			if (theme != vr_theme::default_theme && (entry.page == openxr_ui::panel_renderer::page::save_as || entry.uevr || !entry.all_themes))
				continue;
			openxr_ui::render_input input;
			input.theme = theme;
			input.menu_button = entry.menu_button;
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

	// controls.json: where capture_simulator_ui.ps1 clicks. Compact panel, default theme. Recorded after the PNGs so they
	// stay as they were; the click states below only exist in memory and write no images.
	renderer.set_control_recording(true);
	struct control_page
	{
		std::string name;
		std::vector<openxr_ui::panel_renderer::control_rect> controls;
	};
	std::vector<control_page> pages;
	const auto base_input = [] {
		openxr_ui::render_input input;
		input.preset_dirty = true;
		input.preset_name = "Sharp and Vivid";
		input.eye_width = 4320;
		input.eye_height = 4320;
		return input;
	};
	const auto settle = [&](openxr_ui::render_input input) {
		renderer.render(render_target.Get(), snapshot, input);
		renderer.render(render_target.Get(), snapshot, input);
	};
	const auto record_page = [&](const char *name, openxr_ui::panel_renderer::page page) {
		renderer.show_page(page, 1, "Sharp and Vivid Copy");
		settle(base_input());
		pages.push_back({ name, renderer.recorded_controls() });
	};
	record_page("effects", openxr_ui::panel_renderer::page::effects);
	record_page("detail", openxr_ui::panel_renderer::page::effect_detail);
	record_page("save-as", openxr_ui::panel_renderer::page::save_as);
	record_page("settings", openxr_ui::panel_renderer::page::settings);
	// Each drop-down is opened by clicking its box, like a user, and its open list recorded.
	for (const char *dropdown : { "frame_rate_divisor", "menu_button", "menu_key" })
	{
		renderer.show_page(openxr_ui::panel_renderer::page::settings, 1, "Sharp and Vivid Copy");
		settle(base_input());
		openxr_ui::render_input input = base_input();
		if (!renderer.find_control(dropdown, input.pointer_x, input.pointer_y))
		{
			std::fprintf(stderr, "Control \"%s\" was not drawn on the settings page.\n", dropdown);
			return 1;
		}
		input.pointer_visible = true;
		input.select_clicked = true;
		renderer.render(render_target.Get(), snapshot, input);
		input.select_clicked = false;
		renderer.render(render_target.Get(), snapshot, input);
		pages.push_back({ std::string("settings-") + dropdown + "-open", renderer.recorded_controls() });
	}
	renderer.set_control_recording(false);

	const auto escape = [](const std::string &text) {
		std::string result;
		for (const char character : text)
		{
			if (character == '"' || character == '\\')
				result.push_back('\\');
			result.push_back(character);
		}
		return result;
	};
	std::string json = "{\n";
	for (size_t page = 0; page < pages.size(); ++page)
	{
		json += "\t\"" + escape(pages[page].name) + "\": {\n";
		for (size_t index = 0; index < pages[page].controls.size(); ++index)
		{
			const openxr_ui::panel_renderer::control_rect &control = pages[page].controls[index];
			char numbers[128];
			std::snprintf(numbers, sizeof(numbers), "[%.1f, %.1f, %.1f, %.1f]", control.min_x, control.min_y, control.max_x, control.max_y);
			json += "\t\t\"" + escape(control.id) + "\": " + numbers + (index + 1 < pages[page].controls.size() ? ",\n" : "\n");
		}
		json += std::string("\t}") + (page + 1 < pages.size() ? ",\n" : "\n");
	}
	json += "}\n";
	const std::wstring json_path = output + L"\\controls.json";
	FILE *file = nullptr;
	if (_wfopen_s(&file, json_path.c_str(), L"wb") != 0 || file == nullptr ||
		std::fwrite(json.data(), 1, json.size(), file) != json.size())
	{
		std::fwprintf(stderr, L"Could not write %ls\n", json_path.c_str());
		if (file != nullptr)
			std::fclose(file);
		return 1;
	}
	std::fclose(file);
	std::printf("Wrote controls.json (%zu states).\n", pages.size());
	return 0;
}
