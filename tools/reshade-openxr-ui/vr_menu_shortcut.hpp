#pragma once

#include <cstdint>

namespace openxr_ui
{
	// Controller shortcut that shows or hides the headset menu. settings.ini stores the number, so only append.
	enum menu_button : uint32_t
	{
		menu_button_both_sticks,
		menu_button_hold_both_sticks,
		menu_button_b_and_y,
		menu_button_hold_b_and_y,
		menu_button_hold_left_menu,
		menu_button_off,
		menu_button_count,
	};
	inline constexpr const char *menu_button_names[menu_button_count] = {
		"L3+R3", "Hold L3+R3", "B+Y", "Hold B+Y", "Hold Menu", "Off" };

	// Keyboard key that shows or hides the menu while the game window is in front. settings.ini stores the number.
	enum menu_key : uint32_t
	{
		menu_key_insert,
		menu_key_end,
		menu_key_page_up,
		menu_key_off,
		menu_key_count,
	};
	inline constexpr const char *menu_key_names[menu_key_count] = { "Insert", "End", "Page Up", "Off" };
	// Windows virtual-key codes for each choice; zero for Off.
	inline constexpr int menu_key_codes[menu_key_count] = { 0x2D, 0x23, 0x21, 0 };

	inline constexpr bool menu_button_needs_hold(uint32_t button)
	{
		return button == menu_button_hold_both_sticks || button == menu_button_hold_b_and_y || button == menu_button_hold_left_menu;
	}
}
