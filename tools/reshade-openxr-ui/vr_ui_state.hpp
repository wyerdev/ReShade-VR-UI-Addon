#pragma once

#include "openxr_ui_model.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace reshade::api
{
	struct effect_runtime;
}

bool openxr_ui_ensure_addon_registered();
void openxr_ui_log_message(const char *message, bool error = false);
std::shared_ptr<const openxr_ui::technique_snapshot> openxr_ui_get_techniques();
bool openxr_ui_has_effect_runtime();
bool openxr_ui_uses_effect_runtime(reshade::api::effect_runtime *runtime);
bool openxr_ui_toggle_technique(uint64_t technique_id);
bool openxr_ui_are_shaders_temporarily_disabled();
bool openxr_ui_toggle_all_techniques();
bool openxr_ui_is_preset_dirty();
void openxr_ui_move_active_to_top();
bool openxr_ui_move_technique(uint64_t technique_id, bool move_up);
bool openxr_ui_set_technique_hidden(uint64_t technique_id, bool hidden);
bool openxr_ui_set_technique_favourite(uint64_t technique_id, bool favourite);
bool openxr_ui_get_ui_starts_open();
bool openxr_ui_set_ui_starts_open(bool starts_open);
bool openxr_ui_get_performance_mode();
bool openxr_ui_set_performance_mode(bool enabled);
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