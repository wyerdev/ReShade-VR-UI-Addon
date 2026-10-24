## Which Download Do I Need?

The ZIPs are under **Assets** at the bottom of this page.

| Your game | Download | Install steps |
| --- | --- | --- |
| OpenXR VR game, or an Unreal game played in VR with UEVR | `ReShade-VR-UI-Addon-OpenXR.zip` | **Install steps: OpenXR games and UEVR** below |
| SteamVR game | `ReShade-VR-UI-Addon-SteamVR.zip` | **Install steps: SteamVR games** below |
| Vulkan or 32-bit VR game | No ZIP needed. SteamVR games only need a `ReShadeVR.ini`; the guide shows how to get it | **Install steps: Vulkan and 32-bit games** below |

Each ZIP also has its install steps inside, as `INSTALL.md`.

**SteamVR games:** ReShade's icon only appears in the SteamVR dashboard if the game has a `ReShadeVR.ini` file next to its `.exe`. There are 2 ways, both in the SteamVR ZIP:

- **Automatic (with the add-on):** the add-on writes `ReShadeVR.ini` for you, opens ReShade's menu by itself, and adds an FPS counter.
- **Manual (ReShade alone):** you copy `ReShadeVR.ini` from the ZIP yourself.
