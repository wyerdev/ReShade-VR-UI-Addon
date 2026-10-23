#pragma once

#include "openxr_ui_model.hpp"
#include "vr_frame_statistics.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace reshade::api
{
	struct effect_runtime;
}

bool openxr_ui_ensure_addon_registered();
// True when the ReShade this add-on uses was loaded by the game or a mod, not as an OpenXR API layer.
// The game's ReShade then draws the headset picture: the panel controls its window runtime, and the
// add-on neither starts its effect fallback nor switches off that runtime's effects.
// Do not call from DllMain: the first answer reads the OpenXR layer manifests.
bool openxr_ui_game_brought_own_reshade();
void openxr_ui_log_message(const char *message, bool error = false);
std::shared_ptr<const openxr_ui::technique_snapshot> openxr_ui_get_techniques();
bool openxr_ui_has_effect_runtime();
bool openxr_ui_has_vr_runtime();
bool openxr_ui_uses_effect_runtime(reshade::api::effect_runtime *runtime);
bool openxr_ui_toggle_technique(uint64_t technique_id);
bool openxr_ui_are_shaders_temporarily_disabled();
bool openxr_ui_toggle_all_techniques();
bool openxr_ui_is_preset_dirty();
void openxr_ui_move_active_to_top();
bool openxr_ui_move_technique(uint64_t technique_id, bool move_up);
bool openxr_ui_set_technique_hidden(uint64_t technique_id, bool hidden);
bool openxr_ui_set_technique_favourite(uint64_t technique_id, bool favourite);
bool openxr_ui_revert_technique(uint64_t technique_id);
bool openxr_ui_restore_technique(uint64_t technique_id);
bool openxr_ui_get_ui_starts_open();
bool openxr_ui_set_ui_starts_open(bool starts_open);
bool openxr_ui_get_always_show_statistics();
bool openxr_ui_set_always_show_statistics(bool enabled);
// 1 = full headset rate, 2 = half, 3 = third; colours the FPS graph.
uint32_t openxr_ui_get_frame_rate_divisor();
bool openxr_ui_set_frame_rate_divisor(uint32_t divisor);
// ReShade applies a performance mode change only when the game starts again.
bool openxr_ui_get_performance_mode();
bool openxr_ui_performance_mode_restart_pending();
bool openxr_ui_set_performance_mode(bool enabled);
// ReShade's built-in Generic Depth add-on; ReShade applies the change only when the game starts again.
bool openxr_ui_is_depth_detection_disabled();
bool openxr_ui_depth_detection_restart_pending();
bool openxr_ui_set_depth_detection_disabled(bool disabled);
// Effect files that ReShade skips because an EffectSearchPaths folder does not include its subfolders.
openxr_ui::effect_subfolder_status openxr_ui_get_effect_subfolder_status();
// Adds "\**" to those folders; ReShade's thread saves the change and reloads all effects.
bool openxr_ui_include_effect_subfolders();
// Hides the notice until the game starts again.
void openxr_ui_dismiss_effect_subfolder_notice();
bool openxr_ui_set_parameter(uint64_t parameter_id, uint32_t component, double value);
bool openxr_ui_reset_parameter(uint64_t parameter_id);
bool openxr_ui_reset_all_parameters(uint64_t technique_id);
bool openxr_ui_save_preset();
std::string openxr_ui_get_preset_name();
bool openxr_ui_switch_preset(bool previous);
bool openxr_ui_save_preset_as(const std::string &preset_name);
bool openxr_ui_delete_preset();
int openxr_ui_update_preset_operations();

bool openvr_ui_register_addon(void *module);
void openvr_ui_unregister_overlay();
bool openvr_ui_show_dashboard();
// Zero when SteamVR does not report the headset refresh rate.
float openvr_ui_get_display_frequency();
// Average CPU time over SteamVR's recent frame timings; negative when unavailable. Call rarely; it asks SteamVR.
float openvr_ui_get_cpu_ms();
openxr_ui::frame_statistics openvr_ui_get_frame_statistics();