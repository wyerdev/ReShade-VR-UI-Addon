# ReShade VR UI Add-on

Adds a menu inside your VR headset for ReShade effects. Turn effects on and off, change their settings and **switch presets with head gestures**, without taking the headset off.

## Download

**[⬇ Download the newest version](https://github.com/wyerdev/ReShade-VR-UI-Addon/releases/latest)**

## Which One Do I Need?

| Your game | Install steps |
| --- | --- |
| OpenXR VR game or UEVR | [OpenXR and UEVR](tools/reshade-openxr-ui/INSTALL.md) |
| SteamVR game | [SteamVR](tools/reshade-openxr-ui/INSTALL-SteamVR.md) |
| Vulkan or 32-bit VR game | [Vulkan and 32-bit](tools/reshade-openxr-ui/INSTALL-Vulkan-32bit.md) |

Not sure? See [Which type is my game?](#which-type-is-my-game)

## Features

- **Headset menu (OpenXR games, including UEVR):** turn effects on and off, change their settings, and save or switch presets.
- **Head gesture to switch presets (OpenXR games):** raise a hand to the side of your head and press a button to switch to the next preset, for example from a preset for dark scenes to one for bright scenes.
- **Pin the menu (OpenXR games):** keep it to one side, out of your way.
- **Pin the FPS counter (OpenXR games):** always see your FPS, even with the menu closed.

**SteamVR games** don't get this menu. You use ReShade's own menu from the ReShade icon in the SteamVR dashboard. See the [SteamVR steps](tools/reshade-openxr-ui/INSTALL-SteamVR.md).

![Settings page of the headset menu, with Next preset, Show when menu is closed and the pin button ringed](tools/reshade-openxr-ui/images/menu-settings.png)

![Effects page of the headset menu](tools/reshade-openxr-ui/images/menu-effects.png)

## Which Type Is My Game?

Playing it with UEVR? Then it's **OpenXR**. Otherwise, use one of these two options:

**Option 1: Check the game folder (about a minute)**

1. In Steam, right-click the game, then **Manage** > **Browse local files**.
2. In the search box at the top right of that window, search for `openxr_loader`, then for `openvr_api`.
3. Only `openxr_loader` found: it's **OpenXR**. Only `openvr_api` found: it's **SteamVR**. Both or neither: use Option 2.

To check for Vulkan or 32-bit, look the game up on [PCGamingWiki](https://www.pcgamingwiki.com/) under **API**. Not every game has a page there.

**Option 2: Just try OpenXR**

1. Follow the [OpenXR steps](tools/reshade-openxr-ui/INSTALL.md).
2. If the menu doesn't show up in the headset, it's probably a SteamVR game. Uninstall ReShade for that game in ReShade Setup, then follow the [SteamVR steps](tools/reshade-openxr-ui/INSTALL-SteamVR.md).

You can leave the OpenXR add-on installed. It does nothing in other games.

## Something Not Working?

The end of each install guide has **Help** (unzipping, finding the game folder) and **Troubleshooting**.

## For Developers

Building, testing and releases: [DEVELOPERS.md](DEVELOPERS.md).

## Credits

- [ReShade](https://reshade.me/) by crosire
- [UEVR](https://uevr.io/) by praydog
- [Dear ImGui](https://github.com/ocornut/imgui) by Omar Cornut (the headset menu)
- OpenXR by Khronos, OpenVR by Valve

## License

MIT License, see [LICENSE](LICENSE). ReShade, Dear ImGui, the OpenXR SDK and OpenVR keep their own licenses, see [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt).
