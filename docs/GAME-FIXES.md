# Fixes For Specific Games

Find your game, then follow its fix. For problems in every game, see the **Troubleshooting** part of the [install guide](INSTALL.md).

## Deus Ex: Human Revolution Director's Cut (VR mod)

**No menu or no effects, although ReShade is installed for the game.**

The mod runs VR in a separate program, `DeusExHRVR\DeusExHRVRHost.exe`. ReShade must be installed for that program, not only for `DXHRDC.exe`.

1. Run ReShade Setup and pick `DeusExHRVRHost.exe` in the game's `DeusExHRVR` folder.
2. **Tick** the **OpenXR** layer and finish the install.
3. Start the game.

Tested: with ReShade installed in the `DeusExHRVR` folder, it works.

## UEVR Games

- **No menu:** set UEVR to **OpenXR**, not OpenVR.
- **No effects:** uninstall ReShade from the game folder (or rename its `dxgi.dll`) and use ReShade only as the OpenXR layer.
- **Depth effects (MXAO, depth of field, depth fog) do nothing:** UEVR games give them no depth information. Colour, sharpening and tone effects work.
- **DirectX 12 game crashes or freezes with SteamVR:** it happens even without the add-on (tested: Gothic 1 Remake, Star Wars Jedi: Survivor). There is a workaround in the Flat2VR Discord, where we posted a ReShade DLL we built. Or switch the ReShade layer off in **SteamVR Settings** > **OpenXR** > **Manage OpenXR API Layers** (no ReShade then), or use Pimax's own OpenXR instead of SteamVR. DirectX 11 games work with SteamVR (tested: Star Wars Jedi: Fallen Order).

## Half-Life 2 VR (Vulkan, SteamVR)

Don't use the add-on. ReShade alone works: [steps](INSTALL-Vulkan.md).
