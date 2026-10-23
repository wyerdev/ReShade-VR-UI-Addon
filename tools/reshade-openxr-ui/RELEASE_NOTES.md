## Which Download Do I Need?

| Your game | Download | Install steps |
| --- | --- | --- |
| OpenXR VR game, or an Unreal game played in VR with UEVR | `ReShade-VR-UI-Addon-OpenXR.zip` | [Install for OpenXR and UEVR](https://github.com/wyerdev/ReShade-VR-UI-Addon/blob/main/tools/reshade-openxr-ui/INSTALL.md) |
| SteamVR game | `ReShade-VR-UI-Addon-SteamVR.zip` | [Install for SteamVR](https://github.com/wyerdev/ReShade-VR-UI-Addon/blob/main/tools/reshade-openxr-ui/INSTALL-SteamVR.md) |
| Vulkan or 32-bit VR game | No ZIP needed. SteamVR games only need a `ReShadeVR.ini`; the guide shows how to get it | [ReShade in Vulkan and 32-bit games](https://github.com/wyerdev/ReShade-VR-UI-Addon/blob/main/tools/reshade-openxr-ui/INSTALL-Vulkan-32bit.md) |

Each ZIP also has its install steps inside, as `INSTALL.md`.

**SteamVR games:** ReShade's icon only appears in the SteamVR dashboard if the game has a `ReShadeVR.ini` file next to its `.exe`. There are 2 ways, both in the SteamVR ZIP:

- **Automatic (with the add-on):** the add-on writes `ReShadeVR.ini` for you, opens ReShade's menu by itself, and adds an FPS counter.
- **Manual (ReShade alone):** you copy `ReShadeVR.ini` from the ZIP yourself.
