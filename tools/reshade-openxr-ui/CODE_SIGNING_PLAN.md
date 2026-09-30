# Code Signing And Antivirus Submission Plan

Status: Not started. Saved for later.

## Why

The optional SteamVR loader (`ReShade-VR-Loader.exe`) is an unsigned program that starts with Windows, watches for games and loads DLLs into them. Antivirus tools often treat that pattern as malware, even though ReShade's own injector and the UEVR injector work the same way. Files built locally are not checked as strictly, because Windows only marks downloaded files for extra checks. Once released, users will probably see the "Windows protected your PC" warning, and some antivirus products may quarantine the loader.

Signing proves who published a file and that it was not changed afterwards. It makes antivirus tools more likely to trust the file, but it does not remove the SmartScreen warning straight away. Microsoft says trust still builds up as more people download the file. EV certificates no longer skip that warning either.

## 1. Get A Signing Identity

| Route | Cost | Name shown in the signature | Catch |
| --- | --- | --- | --- |
| SignPath Foundation (signpath.org) | Free for open-source projects | "SignPath Foundation" | You must apply and be accepted. The repository must be public and open source (MIT is fine). Only files built by an online build service such as GitHub Actions can be signed, not local `build.bat` output. |
| Microsoft Artifact Signing (formerly Trusted Signing, in Azure) | Monthly fee; needs a paid Azure subscription | Your verified legal name or company name | Identity check with ID documents. Only some countries are supported. |
| A certificate bought from a certificate company (for example Certum, which has a cheaper open-source option, or Sectigo, DigiCert, SSL.com) | Yearly fee | Your verified legal name | Identity check. The secret signing key must be kept on a USB security key or in the vendor's online service. |

Privacy: the second and third routes put your real name inside every signed file, visible in the file's properties. SignPath avoids that.

Recommendation: SignPath Foundation, because the project is MIT-licensed, it is free, and the publisher's personal name stays out of the files. It needs a public repository and an automatic online build that runs the same steps as `build.bat`.

## 2. Sign The Files

- Sign `ReShade-VR-Loader.exe`, `ReShade-OpenXR-UI.addon64` and `ReShade-SteamVR-UI.addon64`.
- `Install.cmd` and `Uninstall.cmd` cannot be signed; Windows does not support signatures on that file type.
- Sign before zipping. The step would fit at the start of `package.ps1`, as an optional step.
- Always add a timestamp (`/tr`). It keeps the signature valid after the certificate expires.

Certificate on a USB key or in the Windows certificate store:

```powershell
signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 /n "<certificate name>" <file>
```

Microsoft Artifact Signing (Microsoft supplies the `.dll` and a small settings file):

```powershell
signtool sign /fd SHA256 /tr http://timestamp.acs.microsoft.com /td SHA256 /dlib <path>\Azure.CodeSigning.Dlib.dll /dmdf <path>\metadata.json <file>
```

Check the result:

```powershell
signtool verify /pa /v <file>
```

`signtool.exe` comes with the Windows SDK that the Visual Studio build already uses.

## 3. Submit To Microsoft

1. Go to https://www.microsoft.com/wdsi/filesubmission and sign in with a Microsoft account.
2. Choose **Software developer**.
3. Pick the product that complained: **Microsoft Defender Antivirus** or **SmartScreen**.
4. Upload the file or the release zip and choose **Incorrectly detected**. Explain briefly what it does (an open-source ReShade add-on loader for VR games) and link the project.
5. Microsoft replies by email, usually within hours to a few days.

This is mainly useful after an actual warning or detection. Every new build is a new file, so unsigned releases may need submitting again each time; signing reduces that.

## 4. Other Antivirus Products

- Upload the release zip to virustotal.com to see which products flag it.
- Report false positives through each flagging vendor's own form.
- Anything uploaded to VirusTotal is shared with security companies, so only upload files that are public anyway.
