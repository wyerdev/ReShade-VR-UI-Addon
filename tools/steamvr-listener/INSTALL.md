# Install ReShade System-Wide SteamVR Listener

A separate, optional program for 64-bit DirectX 11 and 12 SteamVR games. It is not part of the ReShade VR UI Add-on install.

**What it changes:** you only pick the games in ReShade Setup; when they start, it adds ReShade and the SteamVR add-on (`ReShade-SteamVR-UI.addon64`) to them. The SteamVR add-on is not released on its own for now.

It starts with Windows and uses a little processor time in the background.

Something not working? See [Troubleshooting](#troubleshooting) at the end.

## Install

1. Install **ReShade with full add-on support** from [reshade.me](https://reshade.me/). Normal ReShade does not load add-ons.
2. In ReShade Setup, pick the game's main `.exe`, choose **DirectX 10/11/12**, and tick the **OpenXR layer**. Yes, even though it is a SteamVR game: the listener finds your games through this tick.
3. Close games and SteamVR. Extract the ZIP, then double-click `Install-System-Wide-SteamVR-Listener.cmd`. When Windows asks for permission, click **Yes**.
4. Start the game normally. To check that it worked, open ReShade's menu in the SteamVR dashboard and look for **Add-ons** > **ReShade VR UI Add-on for SteamVR**.

## Update Or Uninstall

- Update: close games and SteamVR, extract the new ZIP, then run `Install-System-Wide-SteamVR-Listener.cmd` and click **Yes** when Windows asks for permission.
- Uninstall: run `Uninstall-System-Wide-SteamVR-Listener.cmd` and click **Yes** when Windows asks for permission. This does not touch the ReShade VR UI Add-on install.

## Where It Installs

- Program files: `C:\Program Files\ReShade System-Wide SteamVR Listener` (the listener and its own copy of the SteamVR add-on). Admin rights are needed to write there.
- Start with Windows: `HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run`, value `ReShade System-Wide SteamVR Listener`.
- Log: `%LOCALAPPDATA%\ReShade System-Wide SteamVR Listener\ReShade-System-Wide-SteamVR-Listener.log`. Uninstalling does not remove it.
- It never changes game folders.

## Troubleshooting

- Game not handled: check that you picked the game's main `.exe` in ReShade Setup with the **OpenXR layer** ticked (step 2).
- Anything else: close the game and delete `ReShade.log` next to the game's `.exe`, so the new log is short. Start the game until the problem shows, close it, then give the new `ReShade.log` and the listener log (path above) to an AI assistant (for example ChatGPT or Copilot) and ask what is wrong.
