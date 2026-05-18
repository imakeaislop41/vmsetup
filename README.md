# VMSetup - AI SLOP

A single-exe tool that fully configures a fresh Windows VM for debugging. No runtime dependencies — just run it as Administrator on a clean install and walk away.

## What It Does

| Step | Description |
|------|-------------|
| **Runtimes** | Installs every common runtime (see [full list](#runtimes-installed)) |
| **Remove Edge** | Fully uninstalls Microsoft Edge and blocks reinstallation |
| **Install Chrome** | Silent enterprise install of Google Chrome |
| **Install DebugView** | Sysinternals DebugView, added to Start Menu |
| **Grey Desktop** | Sets a solid grey wallpaper (no distractions) |
| **Performance** | Aggressive optimization — disables animations, telemetry, indexing, background apps, and more |
| **Disable Defender** | Disables Windows Defender real-time protection, SmartScreen, and cloud reporting |
| **Disable Core Isolation** | Turns off Memory Integrity (HVCI), Credential Guard, Vulnerable Driver Blocklist |
| **Taskbar** | Pins File Explorer, DebugView, PowerShell, Command Prompt, and Chrome |

## Usage

1. Copy `VMSetup.exe` to the target VM
2. Right-click → **Run as administrator**
3. Press Enter to start
4. Reboot when finished

> **Note:** If Tamper Protection is enabled, Defender changes won't fully apply until you manually disable it in Windows Security → Virus & threat protection → Manage settings → Tamper Protection OFF, then reboot or re-run the tool.

## Runtimes Installed

### Visual C++ Redistributables
| Version | Architectures |
|---------|--------------|
| 2005 SP1 | x86, x64 |
| 2008 SP1 | x86, x64 |
| 2010 SP1 | x86, x64 |
| 2012 Update 4 | x86, x64 |
| 2013 | x86, x64 |
| 2015–2022 | x86, x64, ARM64 |

### .NET
- .NET Framework 3.5 (includes 2.0 & 3.0)
- .NET Framework 4.8.1
- .NET 6.0 Desktop Runtime (x86 + x64)
- .NET 7.0 Desktop Runtime (x86 + x64)
- .NET 8.0 Desktop Runtime (x86 + x64)
- .NET 9.0 Desktop Runtime (x86 + x64)
- ASP.NET Core Runtime 6.0, 7.0, 8.0, 9.0

### Java
- Adoptium Temurin JDK 17 LTS
- Adoptium Temurin JDK 21 LTS

### Other
- DirectX End-User Runtime (June 2010)
- XNA Framework 4.0 Refresh
- Visual J# 2.0
- OpenAL 1.1
- Windows Media Foundation (DISM feature)
- WSL (optional feature)

## Performance Optimizations

- Visual effects set to "Best Performance"
- Transparency and animations disabled
- High Performance power plan activated
- SysMain (Superfetch) disabled
- Windows Search indexing disabled
- Telemetry and DiagTrack disabled
- Background apps disabled
- Cortana, Game Bar, Widgets, Copilot disabled
- OneDrive uninstalled
- Hibernation disabled
- Print Spooler disabled
- NTFS optimizations (last-access timestamps off, 8.3 names off)
- Startup delay removed

## Security Changes

**Windows Defender:**
- Real-time monitoring, behavior monitoring, script scanning disabled
- SmartScreen turned off
- Cloud-delivered protection and sample submission disabled
- Defender system tray icon hidden

**Core Isolation / Device Security:**
- Memory Integrity (HVCI) disabled
- Credential Guard disabled
- Vulnerable Driver Blocklist disabled
- Virtualization Based Security disabled

## Building From Source

### Requirements
- Visual Studio 2022 (any edition) with the **Desktop development with C++** workload
- Or just [Visual Studio Build Tools](https://aka.ms/vs/17/release/vs_BuildTools.exe) (free)

### Build
```
build.bat
```

Or from a Developer Command Prompt:
```
cl /O2 /MT /EHsc VMSetup.cpp /Fe:VMSetup.exe /link /SUBSYSTEM:CONSOLE wininet.lib shell32.lib user32.lib advapi32.lib ole32.lib gdi32.lib /MANIFEST:EMBED "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'"
```

The `/MT` flag statically links the C runtime — the resulting exe has **zero external dependencies**.

## Notes

- Designed for disposable debugging VMs, not production machines
- Requires an active internet connection (downloads runtimes and installers)
- Some downloads may fail if Microsoft changes their URLs — the tool continues with remaining steps
- A reboot is required after running for all changes to take full effect
- Taskbar pinning on Windows 11 can be inconsistent — a re-login typically resolves it

## License

MIT
