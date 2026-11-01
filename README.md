# Minecraft: Apple TV Edition — Windows x64 builder

[Download the latest patcher](https://github.com/thebreenis/minecraft-apple-tv-edition-x64/releases/latest)

Build a version of Minecraft: Apple TV Edition that runs on Windows using your own **Minecraft-1.1.5.0.Appx** and **Minecraft_1.1.5_decrypted.tipa**.

## Use

1. Run **MinecraftAppleTVBuilder.exe**. No Python or .NET installation is needed to open the released EXE.
2. Browse for, or drag in, the decrypted Apple TV TIPA and Windows x64 APPX.
3. Choose **Build & Install**, or select **Build APPX only**. Choose a destination; the builder creates a new folder without overwriting existing files.
4. For installation, accept the elevated restart under the **same Windows account**, then click Build & Install again. Nothing installs just by opening the builder.
5. If Windows SDK packaging tools are absent, choose automatic setup through WinGet, or install the SDK manually. The live log shows each build stage.
6. When finished, choose whether to launch. Use the same account in the Xbox app and Microsoft Store, then authenticate the bridge when requested.

The output includes the signed APPX, public certificate, runtime frameworks, **Install.cmd**, instructions and checksums. Install.cmd handles certificate trust and loopback setup on another PC. Neither install path deletes saved worlds or uninstalls another game. A conflicting ordinary Minecraft for Windows installation must be resolved manually after backing up saves.

## Exact supported inputs

Filenames are hints; contents are checked. Both complete archive hashes must match:

| Input | SHA256 |
| --- | --- |
| Windows x64 APPX, identity Microsoft.MinecraftUWP 1.1.500.0 | `67E14091B5E764007E3F246B8D14A123429CBF2D464A1E79A6A525DC2A8BADEB` |
| Decrypted Apple TV 1.1.5 TIPA | `E22F698B96A07E86120813F6559AD1AA097E920FB48D2A206FF4705AF42C9D08` |

The builder does not supply or download either game, decrypt Apple packages, or modify the inputs. A differently repacked TIPA is rejected even if its displayed version matches. The original Windows signature and identity are checked; decrypted Apple packages are not claimed to retain valid Apple signatures.

## What is reconstructed

- The supplied Windows game provides the x64 engine, ordinary assets and resource index.
- Hash-checked byte edits reproduce the engine, manifest, Xbox configuration and UI changes.
- Original bridge/launcher code supplies sign-in, Store compatibility, presence filtering and the three achievement-trigger repairs.
- The Apple TV TIPA provides the title, splash and tile artwork. Square tiles are generated from its background and title rather than redistributing the separately prepared square image. The standalone launcher EXE has no embedded Minecraft icon; its Start entry uses the generated package artwork.
- Five Minecoin images are fetched from pinned public Microsoft asset URLs and verified before conversion. Internet access is required for these five images during a build. No paid content is downloaded or purchased by the builder.
- A public catalog metadata snapshot is included; additional Store artwork is fetched by the running bridge. This is not a bundle of downloadable Marketplace packs.

The obsolete, unused XInputUap.dll from the development staging folder is omitted. The engine imports the included XINPUTATV.dll. Generated signatures and package metadata naturally differ between machines. Build-only produces 1.1.500.210; an installed compatibility build may require a higher package revision for updating, without changing the engine version or patch behavior.

## Certificates, data and dependencies

Each user creates or reuses a **local, non-exportable** signing key. Only the public CER is exported. It lasts ten years; rebuilding after expiry creates a replacement. Its publisher subject matches the original package identity, but it is a community self-signed package, **not a Microsoft-signed Minecraft release**. Trust is limited to Local Computer / Trusted People, not Trusted Root.

Installation adds the package-specific loopback exemption for `Microsoft.MinecraftUWP_8wekyb3d8bbwe`. It does not add hosts entries or disable the firewall. The installed launcher starts its own loopback helpers.

The distribution contains no saved worlds, account tokens, purchase journals, private certificates or finished game APPX. Runtime sessions are stored locally using Windows DPAPI. Automatic expired-token renewal is not implemented; reauthentication may be required. Realms are unavailable. The three achievement repairs were validated in locally hosted worlds; remote-server behavior is not established.

Windows x64, Windows 10 build 17134 or later, or Windows 11 is required. Allow several GB free space plus room for the Windows SDK if missing. Windows 11 was used for development. Microsoft-signed VCLibs and Store Engagement framework APPXs are embedded. The SDK can be obtained from https://developer.microsoft.com/windows/downloads/windows-sdk/; it is not embedded.

Builder files and logs live under `%LOCALAPPDATA%\MinecraftAppleTVBuilder`. Per-build extracted game staging is deleted after success or failure; final output and diagnostic logs are retained. Builds do not touch an existing Xbox bridge session. Do not publish generated game packages or personal build reports.

## Source and rebuilding

- `Source/Frontend`: C# WinForms GUI, adapted from the Mobile builder.
- `Source/Backend`: PowerShell signing/install pipeline, checked patch recipe, public metadata and framework dependencies.
- `Source/Reconstructor`: checked archive extraction, patch application and artwork conversion.
- `Source/Runtime` and `Source/Native`: original compatibility bridge code.

Developer requirements: .NET SDK supporting net8.0-windows, Python 3.11+ x64, Visual Studio C++ Build Tools (including C++/WinRT and MASM), and the Windows SDK. In PowerShell:

```powershell
.\Build-Helpers.ps1 -Python python.exe
.\Publish.ps1
```

Build-Helpers installs the pinned Python build requirements and builds both helpers and the native DLL. Publish embeds the payload in a compressed, self-contained win-x64 EXE. End users need neither compiler toolchain nor Python. Do not commit generated helper binaries; regenerate them from source before Publish on a clean checkout.

Developer verification:

```powershell
.\Release\MinecraftAppleTVBuilder.exe --self-test C:\Temp\selftest.json
.\Release\MinecraftAppleTVBuilder.exe --verify-build C:\Temp\buildtest.json --apple-tv C:\Inputs\Minecraft_1.1.5_decrypted.tipa --desktop C:\Inputs\Minecraft-1.1.5.0.Appx --output C:\Temp\NewBuildFolder
```

The second command uses build-only mode, declines interactive dependency setup, and never installs the game. It still creates a local signing key if one is needed. The destination must not already exist.

## Publishing

Publish source from this folder and place **MinecraftAppleTVBuilder.exe** in a GitHub Release. Do not upload the earlier full-game ZIP. `.gitignore` excludes game archives, certificates, output packages, logs and compiler products; the two Microsoft runtime framework APPXs are deliberate exceptions.

The MIT license covers original project code only. Minecraft assets, Microsoft frameworks, .NET, Python, PyInstaller and other dependencies retain their own licenses. Requiring user-supplied inputs avoids bundling the game; it is not a guarantee of approval by Microsoft, Mojang, Apple or GitHub. This is an unofficial community project.
