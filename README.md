# ReShade VR UI Add-on

Control ReShade's effects from inside your VR headset.

ReShade adds effects like sharpening or colour changes to games. This add-on gives you a menu in the headset to turn them on and off and adjust them, without taking the headset off.

## Download

**[⬇ Download the newest version](https://github.com/wyerdev/ReShade-VR-UI-Addon/releases/latest)**

The download page has the files and the install steps.

## Which One Do I Need?

| Your game | Install steps |
| --- | --- |
| OpenXR VR game, or Unreal game played in VR with UEVR | [OpenXR and UEVR](tools/reshade-openxr-ui/INSTALL.md) |
| SteamVR game | [SteamVR](tools/reshade-openxr-ui/INSTALL-SteamVR.md) |
| Vulkan or 32-bit VR game | [Vulkan and 32-bit](tools/reshade-openxr-ui/INSTALL-Vulkan-32bit.md) (nothing to download) |

Not sure? [Which type is my game?](#which-type-is-my-game) below shows two ways to find out.

## Which Type Is My Game?

Played with UEVR? It's **OpenXR**. Otherwise, pick one of these two options:

**Option 1: Check the game folder (about a minute)**

1. In Steam, right-click the game, then **Manage** > **Browse local files**.
2. In the search box at the top right of that window, search for `openxr_loader`, then for `openvr_api`.
3. Only `openxr_loader` found: it's **OpenXR**. Only `openvr_api` found: it's **SteamVR**. Both or neither: use Option 2.

To check for Vulkan or 32-bit, look the game up on [PCGamingWiki](https://www.pcgamingwiki.com/) under **API**. Not every game has a page there.

**Option 2: Just try OpenXR**

1. Follow the [OpenXR steps](tools/reshade-openxr-ui/INSTALL.md).
2. If no menu shows in the headset, it's probably a SteamVR game. Uninstall ReShade for that game in ReShade Setup, then follow the [SteamVR steps](tools/reshade-openxr-ui/INSTALL-SteamVR.md).

The OpenXR add-on can stay installed; it does nothing in other games.

## Something Not Working?

Each install guide ends with **Help** (unzipping, finding the game folder) and **Troubleshooting**.

## For Developers

Building, testing and releases: [DEVELOPERS.md](DEVELOPERS.md).

## Credits

- [ReShade](https://reshade.me/) by crosire
- [UEVR](https://uevr.io/) by praydog (Unreal games in VR)
- [Dear ImGui](https://github.com/ocornut/imgui) by Omar Cornut (the headset menu)
- OpenXR by Khronos, OpenVR by Valve

## License

MIT License, see [LICENSE](LICENSE). ReShade, Dear ImGui, the OpenXR SDK and OpenVR keep their own licenses, see [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt).
