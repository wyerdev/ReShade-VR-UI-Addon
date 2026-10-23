# Install ReShade System-Wide SteamVR Listener

This is a separate, optional program for 64-bit DirectX 11 and 12 SteamVR (OpenVR) games. It is not part of the ReShade VR UI Add-on install.

Without it, you install ReShade next to each SteamVR game and copy `ReShade-SteamVR-UI.addon64` into that game's folder (Way 1 in the add-on's `INSTALL.md`). With it, you pick games once in ReShade Setup, and the listener adds ReShade and the add-on to them when they start (Way 2). Use only one way per game.

The listener starts with Windows and watches for the games you picked. It uses a little processor time in the background.

## Install

- Install **ReShade with full add-on support** from [reshade.me](https://reshade.me/).
- In ReShade Setup, pick the game's main `.exe`, choose **DirectX 10/11/12**, and enable **OpenXR**. This puts the game on ReShade's central game list, which the listener reads.
- Close games and SteamVR, then right-click `Install-System-Wide-SteamVR-Listener.cmd` and choose **Run as administrator**.
- Start the game normally, then open **Add-ons** > **ReShade VR UI Add-on for SteamVR** in ReShade's SteamVR dashboard.

## Update Or Uninstall

- Update: close games and SteamVR, extract the new ZIP, then run `Install-System-Wide-SteamVR-Listener.cmd` as administrator.
- Uninstall: run `Uninstall-System-Wide-SteamVR-Listener.cmd` as administrator. This does not touch the ReShade VR UI Add-on install.

## Where It Installs

- Program files: `C:\Program Files\ReShade System-Wide SteamVR Listener` (the listener and its own copy of the SteamVR add-on). Admin rights are needed to write there.
- Start with Windows: `HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run`, value `ReShade System-Wide SteamVR Listener`.
- Log: `%LOCALAPPDATA%\ReShade System-Wide SteamVR Listener\ReShade-System-Wide-SteamVR-Listener.log`. Uninstalling does not remove it.
- It never changes game folders.
