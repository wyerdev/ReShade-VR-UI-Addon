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

Not sure? See [Which type is my game?](#which-type-is-my-game) below.

## Which Type Is My Game?

- Played with UEVR: it's **OpenXR**.
- Not sure: try **OpenXR** first. If no menu shows in the headset, it's a SteamVR game. Uninstall ReShade for that game in ReShade Setup, then follow the SteamVR steps. The OpenXR add-on can stay installed; it does nothing in other games.
- Want to check first: search for the game on [PCGamingWiki](https://www.pcgamingwiki.com/). Under **VR support** it lists **OpenXR** or **SteamVR**. Under **API** it shows **Vulkan** and whether the game is 64-bit.

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
