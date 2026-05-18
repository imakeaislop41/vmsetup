#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wininet.h>
#include <shlobj.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "gdi32.lib")

// ─── Helpers ────────────────────────────────────────────────────────────────

static void PrintStep(const char* step) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, 10); // green
    printf("\n[*] %s\n", step);
    SetConsoleTextAttribute(hConsole, 7);  // reset
}

static void PrintInfo(const char* msg) {
    printf("    %s\n", msg);
}

static void PrintError(const char* msg) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, 12); // red
    printf("    [ERROR] %s\n", msg);
    SetConsoleTextAttribute(hConsole, 7);
}

static void PrintSuccess(const char* msg) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, 10);
    printf("    [OK] %s\n", msg);
    SetConsoleTextAttribute(hConsole, 7);
}

static bool RunCommand(const char* cmd, bool wait = true, int timeoutMs = 300000) {
    STARTUPINFOA si = {};
    PROCESS_INFORMATION pi = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    char cmdBuf[4096];
    strncpy_s(cmdBuf, cmd, _TRUNCATE);

    if (!CreateProcessA(NULL, cmdBuf, NULL, NULL, FALSE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        PrintError("Failed to run command");
        PrintInfo(cmd);
        return false;
    }

    if (wait) {
        WaitForSingleObject(pi.hProcess, timeoutMs);
        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return exitCode == 0 || exitCode == 3010; // 3010 = reboot required
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

static bool RunPS(const char* script, bool wait = true, int timeoutMs = 300000) {
    char cmd[8192];
    sprintf_s(cmd, "powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"%s\"", script);
    return RunCommand(cmd, wait, timeoutMs);
}

static bool DownloadFile(const char* url, const char* dest) {
    PrintInfo("Downloading...");
    PrintInfo(url);

    // Use PowerShell for reliable HTTPS downloads
    char ps[2048];
    sprintf_s(ps,
        "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; "
        "Invoke-WebRequest -Uri '%s' -OutFile '%s' -UseBasicParsing",
        url, dest);

    if (RunPS(ps, true, 600000)) {
        PrintSuccess("Download complete");
        return true;
    }

    // Fallback: certutil
    char cmd[2048];
    sprintf_s(cmd, "certutil.exe -urlcache -split -f \"%s\" \"%s\"", url, dest);
    if (RunCommand(cmd, true, 600000)) {
        PrintSuccess("Download complete (certutil fallback)");
        return true;
    }

    PrintError("Download failed");
    return false;
}

static void SetRegistryDWORD(HKEY root, const char* path, const char* name, DWORD value) {
    HKEY hKey;
    if (RegCreateKeyExA(root, path, 0, NULL, 0, KEY_SET_VALUE | KEY_WOW64_64KEY,
                        NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExA(hKey, name, 0, REG_DWORD, (BYTE*)&value, sizeof(value));
        RegCloseKey(hKey);
    }
}

static void SetRegistryString(HKEY root, const char* path, const char* name, const char* value) {
    HKEY hKey;
    if (RegCreateKeyExA(root, path, 0, NULL, 0, KEY_SET_VALUE | KEY_WOW64_64KEY,
                        NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExA(hKey, name, 0, REG_SZ, (BYTE*)value, (DWORD)strlen(value) + 1);
        RegCloseKey(hKey);
    }
}

static void DeleteRegistryKey(HKEY root, const char* path) {
    RegDeleteTreeA(root, path);
}

static void StopService(const char* serviceName) {
    char cmd[512];
    sprintf_s(cmd, "sc.exe stop \"%s\"", serviceName);
    RunCommand(cmd);
    sprintf_s(cmd, "sc.exe config \"%s\" start= disabled", serviceName);
    RunCommand(cmd);
}

static void EnsureTempDir() {
    _mkdir("C:\\VMSetup_Temp");
}

// ─── Step 1: Install Runtimes ───────────────────────────────────────────────

struct RuntimeInfo {
    const char* name;
    const char* url;
    const char* filename;
    const char* silentArgs;
};

static void InstallRuntime(const RuntimeInfo& rt) {
    PrintInfo(rt.name);
    char destPath[512];
    sprintf_s(destPath, "C:\\VMSetup_Temp\\%s", rt.filename);
    if (DownloadFile(rt.url, destPath)) {
        char cmd[1024];
        sprintf_s(cmd, "\"%s\" %s", destPath, rt.silentArgs);
        if (RunCommand(cmd, true, 600000)) PrintSuccess(rt.name);
        else PrintError(rt.name);
    }
}

static void InstallMSI(const char* name, const char* url, const char* filename) {
    PrintInfo(name);
    char destPath[512];
    sprintf_s(destPath, "C:\\VMSetup_Temp\\%s", filename);
    if (DownloadFile(url, destPath)) {
        char cmd[1024];
        sprintf_s(cmd, "msiexec.exe /i \"%s\" /quiet /norestart", destPath);
        if (RunCommand(cmd, true, 600000)) PrintSuccess(name);
        else PrintError(name);
    }
}

static void InstallRuntimes() {
    PrintStep("Installing ALL runtimes...");
    EnsureTempDir();

    // ════════════════════════════════════════════════════════════════
    //  Visual C++ Redistributables - ALL versions, x86 + x64
    // ════════════════════════════════════════════════════════════════

    PrintStep("Installing Visual C++ Redistributables (all versions)...");

    RuntimeInfo vcRuntimes[] = {
        // VC++ 2005 SP1 (8.0)
        {"VC++ 2005 SP1 x86",
         "https://download.microsoft.com/download/8/B/4/8B42259F-5D70-43F4-AC2E-4B208FD8D66A/vcredist_x86.EXE",
         "vcredist2005_x86.exe", "/q"},
        {"VC++ 2005 SP1 x64",
         "https://download.microsoft.com/download/8/B/4/8B42259F-5D70-43F4-AC2E-4B208FD8D66A/vcredist_x64.EXE",
         "vcredist2005_x64.exe", "/q"},

        // VC++ 2008 SP1 (9.0)
        {"VC++ 2008 SP1 x86",
         "https://download.microsoft.com/download/5/D/8/5D8C65CB-C849-4025-8E95-C3966CAFD8AE/vcredist_x86.exe",
         "vcredist2008_x86.exe", "/q"},
        {"VC++ 2008 SP1 x64",
         "https://download.microsoft.com/download/5/D/8/5D8C65CB-C849-4025-8E95-C3966CAFD8AE/vcredist_x64.exe",
         "vcredist2008_x64.exe", "/q"},

        // VC++ 2010 SP1 (10.0)
        {"VC++ 2010 SP1 x86",
         "https://download.microsoft.com/download/1/6/5/165255E7-1014-4D0A-B094-B6A430A6BFFC/vcredist_x86.exe",
         "vcredist2010_x86.exe", "/passive /norestart"},
        {"VC++ 2010 SP1 x64",
         "https://download.microsoft.com/download/1/6/5/165255E7-1014-4D0A-B094-B6A430A6BFFC/vcredist_x64.exe",
         "vcredist2010_x64.exe", "/passive /norestart"},

        // VC++ 2012 Update 4 (11.0)
        {"VC++ 2012 Update 4 x86",
         "https://download.microsoft.com/download/1/6/B/16B06F60-3B20-4FF2-B699-5E9B7962F9AE/VSU_4/vcredist_x86.exe",
         "vcredist2012_x86.exe", "/install /quiet /norestart"},
        {"VC++ 2012 Update 4 x64",
         "https://download.microsoft.com/download/1/6/B/16B06F60-3B20-4FF2-B699-5E9B7962F9AE/VSU_4/vcredist_x64.exe",
         "vcredist2012_x64.exe", "/install /quiet /norestart"},

        // VC++ 2013 (12.0)
        {"VC++ 2013 x86",
         "https://aka.ms/highdpimfc2013x86enu",
         "vcredist2013_x86.exe", "/install /quiet /norestart"},
        {"VC++ 2013 x64",
         "https://aka.ms/highdpimfc2013x64enu",
         "vcredist2013_x64.exe", "/install /quiet /norestart"},

        // VC++ 2015-2022 (14.x) - latest, covers 2015/2017/2019/2022
        {"VC++ 2015-2022 x86",
         "https://aka.ms/vs/17/release/vc_redist.x86.exe",
         "vcredist2022_x86.exe", "/install /quiet /norestart"},
        {"VC++ 2015-2022 x64",
         "https://aka.ms/vs/17/release/vc_redist.x64.exe",
         "vcredist2022_x64.exe", "/install /quiet /norestart"},
        {"VC++ 2015-2022 ARM64",
         "https://aka.ms/vs/17/release/vc_redist.arm64.exe",
         "vcredist2022_arm64.exe", "/install /quiet /norestart"},
    };

    for (const auto& rt : vcRuntimes) {
        InstallRuntime(rt);
    }

    // ════════════════════════════════════════════════════════════════
    //  .NET Framework
    // ════════════════════════════════════════════════════════════════

    PrintStep("Installing .NET Framework runtimes...");

    // .NET Framework 3.5 (includes 2.0 and 3.0) - Windows feature
    PrintInfo(".NET Framework 3.5 (includes 2.0 & 3.0) via DISM...");
    if (RunCommand("dism.exe /online /enable-feature /featurename:NetFx3 /all /norestart", true, 600000))
        PrintSuccess(".NET Framework 3.5 enabled");
    else
        PrintError(".NET Framework 3.5 (may need Windows media/internet)");

    // .NET Framework 4.8.1
    RuntimeInfo dotnetFx[] = {
        {".NET Framework 4.8.1",
         "https://go.microsoft.com/fwlink/?LinkId=2203304",
         "ndp481-x86-x64-allos-enu.exe", "/quiet /norestart"},
    };
    for (const auto& rt : dotnetFx) {
        InstallRuntime(rt);
    }

    // ════════════════════════════════════════════════════════════════
    //  .NET (Modern) Desktop Runtimes - 6.0, 7.0, 8.0, 9.0
    // ════════════════════════════════════════════════════════════════

    PrintStep("Installing .NET Desktop Runtimes (modern)...");

    RuntimeInfo dotnetModern[] = {
        // .NET 6.0 LTS
        {".NET 6.0 Desktop Runtime x64",
         "https://aka.ms/dotnet/6.0/windowsdesktop-runtime-win-x64.exe",
         "dotnet6_desktop_x64.exe", "/install /quiet /norestart"},
        {".NET 6.0 Desktop Runtime x86",
         "https://aka.ms/dotnet/6.0/windowsdesktop-runtime-win-x86.exe",
         "dotnet6_desktop_x86.exe", "/install /quiet /norestart"},

        // .NET 7.0
        {".NET 7.0 Desktop Runtime x64",
         "https://aka.ms/dotnet/7.0/windowsdesktop-runtime-win-x64.exe",
         "dotnet7_desktop_x64.exe", "/install /quiet /norestart"},
        {".NET 7.0 Desktop Runtime x86",
         "https://aka.ms/dotnet/7.0/windowsdesktop-runtime-win-x86.exe",
         "dotnet7_desktop_x86.exe", "/install /quiet /norestart"},

        // .NET 8.0 LTS
        {".NET 8.0 Desktop Runtime x64",
         "https://aka.ms/dotnet/8.0/windowsdesktop-runtime-win-x64.exe",
         "dotnet8_desktop_x64.exe", "/install /quiet /norestart"},
        {".NET 8.0 Desktop Runtime x86",
         "https://aka.ms/dotnet/8.0/windowsdesktop-runtime-win-x86.exe",
         "dotnet8_desktop_x86.exe", "/install /quiet /norestart"},

        // .NET 9.0
        {".NET 9.0 Desktop Runtime x64",
         "https://aka.ms/dotnet/9.0/windowsdesktop-runtime-win-x64.exe",
         "dotnet9_desktop_x64.exe", "/install /quiet /norestart"},
        {".NET 9.0 Desktop Runtime x86",
         "https://aka.ms/dotnet/9.0/windowsdesktop-runtime-win-x86.exe",
         "dotnet9_desktop_x86.exe", "/install /quiet /norestart"},
    };

    for (const auto& rt : dotnetModern) {
        InstallRuntime(rt);
    }

    // ════════════════════════════════════════════════════════════════
    //  ASP.NET Core Runtimes (for web apps / Kestrel)
    // ════════════════════════════════════════════════════════════════

    PrintStep("Installing ASP.NET Core Runtimes...");

    RuntimeInfo aspnetRuntimes[] = {
        {"ASP.NET Core 6.0 Runtime x64",
         "https://aka.ms/dotnet/6.0/aspnetcore-runtime-win-x64.exe",
         "aspnet6_x64.exe", "/install /quiet /norestart"},
        {"ASP.NET Core 7.0 Runtime x64",
         "https://aka.ms/dotnet/7.0/aspnetcore-runtime-win-x64.exe",
         "aspnet7_x64.exe", "/install /quiet /norestart"},
        {"ASP.NET Core 8.0 Runtime x64",
         "https://aka.ms/dotnet/8.0/aspnetcore-runtime-win-x64.exe",
         "aspnet8_x64.exe", "/install /quiet /norestart"},
        {"ASP.NET Core 9.0 Runtime x64",
         "https://aka.ms/dotnet/9.0/aspnetcore-runtime-win-x64.exe",
         "aspnet9_x64.exe", "/install /quiet /norestart"},
    };

    for (const auto& rt : aspnetRuntimes) {
        InstallRuntime(rt);
    }

    // ════════════════════════════════════════════════════════════════
    //  Java Runtime (Adoptium/Temurin JDK 21 LTS + JDK 17 LTS)
    // ════════════════════════════════════════════════════════════════

    PrintStep("Installing Java Runtimes...");

    // Adoptium Temurin JDK 17 LTS
    PrintInfo("Java JDK 17 LTS (Adoptium Temurin) x64...");
    InstallMSI("Java JDK 17 LTS x64",
        "https://github.com/adoptium/temurin17-binaries/releases/download/jdk-17.0.13%2B11/OpenJDK17U-jdk_x64_windows_hotspot_17.0.13_11.msi",
        "temurin_jdk17_x64.msi");

    // Adoptium Temurin JDK 21 LTS
    PrintInfo("Java JDK 21 LTS (Adoptium Temurin) x64...");
    InstallMSI("Java JDK 21 LTS x64",
        "https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.5%2B11/OpenJDK21U-jdk_x64_windows_hotspot_21.0.5_11.msi",
        "temurin_jdk21_x64.msi");

    // ════════════════════════════════════════════════════════════════
    //  DirectX End-User Runtime (June 2010 - legacy d3dx9, etc.)
    // ════════════════════════════════════════════════════════════════

    PrintStep("Installing DirectX End-User Runtime...");

    PrintInfo("DirectX End-User Runtime (June 2010)...");
    const char* dxUrl = "https://download.microsoft.com/download/8/4/A/84A35BF1-DAFE-4AE8-82AF-AD2AE20B6B14/directx_Jun2010_redist.exe";
    const char* dxPath = "C:\\VMSetup_Temp\\directx_redist.exe";
    if (DownloadFile(dxUrl, dxPath)) {
        _mkdir("C:\\VMSetup_Temp\\DirectX");
        char cmd[512];
        sprintf_s(cmd, "\"%s\" /Q /T:C:\\VMSetup_Temp\\DirectX", dxPath);
        if (RunCommand(cmd, true, 120000)) {
            RunCommand("C:\\VMSetup_Temp\\DirectX\\DXSETUP.exe /silent", true, 120000);
            PrintSuccess("DirectX End-User Runtime installed");
        }
    }

    // ════════════════════════════════════════════════════════════════
    //  XNA Framework 4.0 Refresh
    // ════════════════════════════════════════════════════════════════

    PrintStep("Installing XNA Framework...");

    InstallMSI("XNA Framework 4.0 Refresh",
        "https://download.microsoft.com/download/A/C/2/AC2C903B-E6E8-42C2-9FD7-BEBAC362A930/xnafx40_redist.msi",
        "xnafx40_redist.msi");

    // ════════════════════════════════════════════════════════════════
    //  Visual J# 2.0 Redistributable (legacy)
    // ════════════════════════════════════════════════════════════════

    PrintStep("Installing Visual J# 2.0...");

    InstallMSI("Visual J# 2.0 x64",
        "https://download.microsoft.com/download/9/2/8/9288E955-EC0F-4827-84E0-B04B3A517AB0/vjredist64.exe",
        "vjredist64.exe");

    // ════════════════════════════════════════════════════════════════
    //  OpenAL (used by many games/apps)
    // ════════════════════════════════════════════════════════════════

    PrintStep("Installing OpenAL...");

    PrintInfo("OpenAL 1.1 Installer...");
    const char* openalUrl = "https://www.openal.org/downloads/oalinst.zip";
    const char* openalZip = "C:\\VMSetup_Temp\\oalinst.zip";
    if (DownloadFile(openalUrl, openalZip)) {
        RunPS(
            "Expand-Archive -Path 'C:\\VMSetup_Temp\\oalinst.zip' -DestinationPath 'C:\\VMSetup_Temp\\OpenAL' -Force; "
            "$installer = Get-ChildItem 'C:\\VMSetup_Temp\\OpenAL' -Filter '*.exe' -Recurse | Select-Object -First 1; "
            "if ($installer) { Start-Process $installer.FullName -ArgumentList '/SILENT' -Wait }"
        );
        PrintSuccess("OpenAL installed");
    }

    // ════════════════════════════════════════════════════════════════
    //  Windows Optional Features
    // ════════════════════════════════════════════════════════════════

    PrintStep("Enabling Windows optional features...");

    // .NET Framework 3.5 already done above

    // Windows Media Foundation (for media codecs)
    PrintInfo("Enabling Media Foundation...");
    RunCommand("dism.exe /online /enable-feature /featurename:MediaPlayback /all /norestart", true, 120000);

    // Windows Subsystem for Linux (useful for debugging)
    PrintInfo("Enabling WSL optional feature...");
    RunCommand("dism.exe /online /enable-feature /featurename:Microsoft-Windows-Subsystem-Linux /all /norestart", true, 120000);

    // ════════════════════════════════════════════════════════════════

    PrintSuccess("All runtime installations complete!");
}

// ─── Step 2: Remove Microsoft Edge ──────────────────────────────────────────

static void RemoveEdge() {
    PrintStep("Removing Microsoft Edge...");

    // Stop Edge processes
    RunCommand("taskkill.exe /F /IM msedge.exe", true, 10000);
    RunCommand("taskkill.exe /F /IM MicrosoftEdgeUpdate.exe", true, 10000);

    // Use Edge's own installer to uninstall (if available)
    PrintInfo("Attempting uninstall via Edge setup...");
    RunPS(
        "$edgePaths = @("
        "  'C:\\Program Files (x86)\\Microsoft\\Edge\\Application',"
        "  'C:\\Program Files\\Microsoft\\Edge\\Application'"
        "); "
        "foreach ($base in $edgePaths) {"
        "  if (Test-Path $base) {"
        "    $versions = Get-ChildItem $base -Directory | Where-Object { $_.Name -match '^\\d+' }; "
        "    foreach ($v in $versions) {"
        "      $setup = Join-Path $v.FullName 'Installer\\setup.exe'; "
        "      if (Test-Path $setup) {"
        "        Start-Process $setup -ArgumentList '--uninstall','--system-level','--force-uninstall' -Wait -NoNewWindow; "
        "      }"
        "    }"
        "  }"
        "}"
    );

    // Prevent Edge from reinstalling
    SetRegistryDWORD(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Microsoft\\EdgeUpdate", "DoNotUpdateToEdgeWithChromium", 1);

    // Remove Edge Update
    StopService("edgeupdate");
    StopService("edgeupdatem");
    StopService("MicrosoftEdgeElevationService");

    // Remove scheduled tasks for Edge
    RunCommand("schtasks.exe /Delete /TN \"MicrosoftEdgeUpdateTaskMachineCore\" /F", true, 10000);
    RunCommand("schtasks.exe /Delete /TN \"MicrosoftEdgeUpdateTaskMachineUA\" /F", true, 10000);

    // Remove Edge directories
    RunPS(
        "Remove-Item -Path 'C:\\Program Files (x86)\\Microsoft\\Edge' -Recurse -Force -ErrorAction SilentlyContinue; "
        "Remove-Item -Path 'C:\\Program Files\\Microsoft\\Edge' -Recurse -Force -ErrorAction SilentlyContinue; "
        "Remove-Item -Path 'C:\\Program Files (x86)\\Microsoft\\EdgeUpdate' -Recurse -Force -ErrorAction SilentlyContinue; "
        "Remove-Item -Path 'C:\\Program Files\\Microsoft\\EdgeUpdate' -Recurse -Force -ErrorAction SilentlyContinue; "
        "Remove-Item -Path \"$env:ProgramData\\Microsoft\\EdgeUpdate\" -Recurse -Force -ErrorAction SilentlyContinue; "
        "Remove-Item -Path \"$env:LOCALAPPDATA\\Microsoft\\Edge\" -Recurse -Force -ErrorAction SilentlyContinue; "
        "Remove-Item -Path 'C:\\Windows\\SystemApps\\Microsoft.MicrosoftEdge*' -Recurse -Force -ErrorAction SilentlyContinue"
    );

    // Remove Edge from registry
    DeleteRegistryKey(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Edge");
    DeleteRegistryKey(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Microsoft\\Edge");

    // Block Edge reinstallation via Windows Update
    SetRegistryDWORD(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Orchestrator\\UScheduler\\EdgeUpdate",
        "workCompleted", 1);

    PrintSuccess("Microsoft Edge removal complete");
}

// ─── Step 3: Install Chrome ─────────────────────────────────────────────────

static void InstallChrome() {
    PrintStep("Installing Google Chrome...");
    EnsureTempDir();

    const char* chromeUrl = "https://dl.google.com/chrome/install/GoogleChromeStandaloneEnterprise64.msi";
    const char* chromePath = "C:\\VMSetup_Temp\\chrome.msi";

    if (DownloadFile(chromeUrl, chromePath)) {
        char cmd[512];
        sprintf_s(cmd, "msiexec.exe /i \"%s\" /quiet /norestart", chromePath);
        if (RunCommand(cmd, true, 600000)) {
            PrintSuccess("Google Chrome installed");
        } else {
            PrintError("Chrome installation may have failed");
        }
    }
}

// ─── Step 4: Install DebugView ──────────────────────────────────────────────

static void InstallDebugView() {
    PrintStep("Installing DebugView...");
    EnsureTempDir();

    const char* dbgViewUrl = "https://download.sysinternals.com/files/DebugView.zip";
    const char* dbgViewZip = "C:\\VMSetup_Temp\\DebugView.zip";
    const char* dbgViewDir = "C:\\Program Files\\DebugView";

    if (DownloadFile(dbgViewUrl, dbgViewZip)) {
        // Extract
        char ps[1024];
        sprintf_s(ps,
            "New-Item -ItemType Directory -Path '%s' -Force | Out-Null; "
            "Expand-Archive -Path '%s' -DestinationPath '%s' -Force",
            dbgViewDir, dbgViewZip, dbgViewDir);
        RunPS(ps);

        // Create shortcut in Start Menu (makes it a "normal application")
        RunPS(
            "$ws = New-Object -ComObject WScript.Shell; "
            "$shortcut = $ws.CreateShortcut(\"$env:ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\DebugView.lnk\"); "
            "$shortcut.TargetPath = 'C:\\Program Files\\DebugView\\Dbgview.exe'; "
            "$shortcut.WorkingDirectory = 'C:\\Program Files\\DebugView'; "
            "$shortcut.Description = 'Sysinternals DebugView'; "
            "$shortcut.Save()"
        );

        PrintSuccess("DebugView installed to C:\\Program Files\\DebugView");
    }
}

// ─── Step 5: Set Grey Background ────────────────────────────────────────────

static void SetGreyBackground() {
    PrintStep("Setting desktop background to grey...");

    // Remove current wallpaper, set solid color
    SetRegistryString(HKEY_CURRENT_USER,
        "Control Panel\\Desktop", "WallPaper", "");
    SetRegistryDWORD(HKEY_CURRENT_USER,
        "Control Panel\\Desktop", "WallpaperStyle", 0);
    SetRegistryString(HKEY_CURRENT_USER,
        "Control Panel\\Desktop", "TileWallpaper", "0");

    // Set solid grey background color (RGB: 128, 128, 128)
    SetRegistryString(HKEY_CURRENT_USER,
        "Control Panel\\Colors", "Background", "128 128 128");

    // Apply immediately
    SystemParametersInfoA(SPI_SETDESKWALLPAPER, 0, (PVOID)"", SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);

    // Also set via PowerShell for reliability
    RunPS(
        "Set-ItemProperty -Path 'HKCU:\\Control Panel\\Desktop' -Name WallPaper -Value ''; "
        "Set-ItemProperty -Path 'HKCU:\\Control Panel\\Colors' -Name Background -Value '128 128 128'; "
        "Add-Type -TypeDefinition '"
        "  using System; using System.Runtime.InteropServices;"
        "  public class Wallpaper {"
        "    [DllImport(\"user32.dll\", CharSet=CharSet.Auto)]"
        "    public static extern int SystemParametersInfo(int uAction, int uParam, string lpvParam, int fuWinIni);"
        "  }'; "
        "[Wallpaper]::SystemParametersInfo(20, 0, '', 3)"
    );

    PrintSuccess("Desktop background set to grey");
}

// ─── Step 6: Performance Optimizations ──────────────────────────────────────

static void OptimizePerformance() {
    PrintStep("Applying performance optimizations...");

    // ── Visual Effects: Best Performance ──
    PrintInfo("Disabling visual effects...");
    SetRegistryDWORD(HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VisualEffects", "VisualFXSetting", 2);

    // Disable individual visual effects
    const char* dwmPath = "Software\\Microsoft\\Windows\\DWM";
    SetRegistryDWORD(HKEY_CURRENT_USER, dwmPath, "EnableAeroPeek", 0);
    SetRegistryDWORD(HKEY_CURRENT_USER, dwmPath, "AlwaysHibernateThumbnails", 0);

    const char* explorerAdv = "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";
    SetRegistryDWORD(HKEY_CURRENT_USER, explorerAdv, "TaskbarAnimations", 0);
    SetRegistryDWORD(HKEY_CURRENT_USER, explorerAdv, "ListviewShadow", 0);
    SetRegistryDWORD(HKEY_CURRENT_USER, explorerAdv, "IconsOnly", 0);

    // Disable animations
    SetRegistryString(HKEY_CURRENT_USER,
        "Control Panel\\Desktop", "UserPreferencesMask",
        "\x90\x12\x03\x80\x10\x00\x00\x00"); // best performance bitmask
    SetRegistryString(HKEY_CURRENT_USER,
        "Control Panel\\Desktop\\WindowMetrics", "MinAnimate", "0");
    SetRegistryDWORD(HKEY_CURRENT_USER,
        "Control Panel\\Desktop", "DragFullWindows", 0);
    SetRegistryDWORD(HKEY_CURRENT_USER,
        "Control Panel\\Desktop", "MenuShowDelay", 0);

    // ── Disable Transparency ──
    PrintInfo("Disabling transparency effects...");
    SetRegistryDWORD(HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", "EnableTransparency", 0);

    // ── Power Plan: High Performance ──
    PrintInfo("Setting High Performance power plan...");
    RunCommand("powercfg.exe /setactive 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c");
    // Also create Ultimate Performance if available
    RunCommand("powercfg.exe -duplicatescheme e9a42b02-d5df-448d-aa00-03f14749eb61", true, 10000);

    // ── Disable Superfetch/SysMain ──
    PrintInfo("Disabling SysMain (Superfetch)...");
    StopService("SysMain");

    // ── Disable Windows Search Indexing ──
    PrintInfo("Disabling Windows Search indexing...");
    StopService("WSearch");

    // ── Disable Background Apps ──
    PrintInfo("Disabling background apps...");
    SetRegistryDWORD(HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\BackgroundAccessApplications",
        "GlobalUserDisabled", 1);
    SetRegistryDWORD(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Policies\\Microsoft\\Windows\\AppPrivacy",
        "LetAppsRunInBackground", 2);

    // ── Disable Cortana ──
    PrintInfo("Disabling Cortana...");
    SetRegistryDWORD(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Policies\\Microsoft\\Windows\\Windows Search", "AllowCortana", 0);

    // ── Disable Telemetry ──
    PrintInfo("Reducing telemetry...");
    SetRegistryDWORD(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection", "AllowTelemetry", 0);
    StopService("DiagTrack");
    StopService("dmwappushservice");

    // ── Disable Tips & Suggestions ──
    SetRegistryDWORD(HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager",
        "SubscribedContent-338389Enabled", 0);
    SetRegistryDWORD(HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager",
        "SubscribedContent-310093Enabled", 0);
    SetRegistryDWORD(HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager",
        "SilentInstalledAppsEnabled", 0);

    // ── Disable Game Bar & Game DVR ──
    PrintInfo("Disabling Game Bar...");
    SetRegistryDWORD(HKEY_CURRENT_USER,
        "Software\\Microsoft\\GameBar", "AutoGameModeEnabled", 0);
    SetRegistryDWORD(HKEY_CURRENT_USER,
        "System\\GameConfigStore", "GameDVR_Enabled", 0);
    SetRegistryDWORD(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Policies\\Microsoft\\Windows\\GameDVR", "AllowGameDVR", 0);

    // ── Disable Hibernation ──
    PrintInfo("Disabling hibernation...");
    RunCommand("powercfg.exe /hibernate off");

    // ── Disable Print Spooler (not needed in debug VMs usually) ──
    PrintInfo("Disabling Print Spooler...");
    StopService("Spooler");

    // ── Disable Widgets ──
    SetRegistryDWORD(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Policies\\Microsoft\\Dsh", "AllowNewsAndInterests", 0);

    // ── Disable Windows Copilot ──
    SetRegistryDWORD(HKEY_CURRENT_USER,
        "Software\\Policies\\Microsoft\\Windows\\WindowsCopilot", "TurnOffWindowsCopilot", 1);

    // ── Disable OneDrive ──
    PrintInfo("Disabling OneDrive...");
    RunCommand("taskkill.exe /F /IM OneDrive.exe", true, 5000);
    RunCommand("\"%SystemRoot%\\System32\\OneDriveSetup.exe\" /uninstall", true, 30000);
    RunCommand("\"%SystemRoot%\\SysWOW64\\OneDriveSetup.exe\" /uninstall", true, 30000);

    // ── Disable Startup Delay ──
    SetRegistryDWORD(HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Serialize",
        "StartupDelayInMSec", 0);

    // ── Optimize NTFS ──
    PrintInfo("Optimizing NTFS...");
    RunCommand("fsutil behavior set disablelastaccess 1");
    RunCommand("fsutil behavior set disable8dot3 1");

    // ── Disable Remote Assistance ──
    SetRegistryDWORD(HKEY_LOCAL_MACHINE,
        "SYSTEM\\CurrentControlSet\\Control\\Remote Assistance", "fAllowToGetHelp", 0);

    // ── Set processor scheduling to Programs (foreground) ──
    SetRegistryDWORD(HKEY_LOCAL_MACHINE,
        "SYSTEM\\CurrentControlSet\\Control\\PriorityControl", "Win32PrioritySeparation", 38);

    PrintSuccess("Performance optimizations applied");
}

// ─── Step 7: Disable Windows Defender ───────────────────────────────────────

static void DisableDefender() {
    PrintStep("Disabling Windows Defender...");

    // Disable via Group Policy registry keys
    const char* defenderPolicy = "SOFTWARE\\Policies\\Microsoft\\Windows Defender";
    SetRegistryDWORD(HKEY_LOCAL_MACHINE, defenderPolicy, "DisableAntiSpyware", 1);
    SetRegistryDWORD(HKEY_LOCAL_MACHINE, defenderPolicy, "DisableAntiVirus", 1);
    SetRegistryDWORD(HKEY_LOCAL_MACHINE, defenderPolicy, "ServiceKeepAlive", 0);

    char rtpPath[256];
    sprintf_s(rtpPath, "%s\\Real-Time Protection", defenderPolicy);
    SetRegistryDWORD(HKEY_LOCAL_MACHINE, rtpPath, "DisableRealtimeMonitoring", 1);
    SetRegistryDWORD(HKEY_LOCAL_MACHINE, rtpPath, "DisableBehaviorMonitoring", 1);
    SetRegistryDWORD(HKEY_LOCAL_MACHINE, rtpPath, "DisableOnAccessProtection", 1);
    SetRegistryDWORD(HKEY_LOCAL_MACHINE, rtpPath, "DisableScanOnRealtimeEnable", 1);
    SetRegistryDWORD(HKEY_LOCAL_MACHINE, rtpPath, "DisableIOAVProtection", 1);

    // Disable SpyNet/MAPS
    char spynetPath[256];
    sprintf_s(spynetPath, "%s\\Spynet", defenderPolicy);
    SetRegistryDWORD(HKEY_LOCAL_MACHINE, spynetPath, "SpynetReporting", 0);
    SetRegistryDWORD(HKEY_LOCAL_MACHINE, spynetPath, "SubmitSamplesConsent", 2);

    // Disable Cloud Protection
    char mpEngine[256];
    sprintf_s(mpEngine, "%s\\MpEngine", defenderPolicy);
    SetRegistryDWORD(HKEY_LOCAL_MACHINE, mpEngine, "MpCloudBlockLevel", 0);

    // Disable Defender services
    StopService("WinDefend");
    StopService("WdNisSvc");
    StopService("SecurityHealthService");

    // Disable via PowerShell (in case Tamper Protection is off)
    RunPS(
        "try { Set-MpPreference -DisableRealtimeMonitoring $true -ErrorAction SilentlyContinue } catch {}; "
        "try { Set-MpPreference -DisableBehaviorMonitoring $true -ErrorAction SilentlyContinue } catch {}; "
        "try { Set-MpPreference -DisableBlockAtFirstSeen $true -ErrorAction SilentlyContinue } catch {}; "
        "try { Set-MpPreference -DisableIOAVProtection $true -ErrorAction SilentlyContinue } catch {}; "
        "try { Set-MpPreference -DisablePrivacyMode $true -ErrorAction SilentlyContinue } catch {}; "
        "try { Set-MpPreference -DisableScriptScanning $true -ErrorAction SilentlyContinue } catch {}; "
        "try { Set-MpPreference -LowThreatDefaultAction 6 -ErrorAction SilentlyContinue } catch {}; "
        "try { Set-MpPreference -ModerateThreatDefaultAction 6 -ErrorAction SilentlyContinue } catch {}; "
        "try { Set-MpPreference -HighThreatDefaultAction 6 -ErrorAction SilentlyContinue } catch {}; "
        "try { Set-MpPreference -SevereThreatDefaultAction 6 -ErrorAction SilentlyContinue } catch {}; "
        "try { Set-MpPreference -EnableControlledFolderAccess Disabled -ErrorAction SilentlyContinue } catch {}"
    );

    // Disable SmartScreen
    SetRegistryDWORD(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Policies\\Microsoft\\Windows\\System", "EnableSmartScreen", 0);
    SetRegistryString(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer", "SmartScreenEnabled", "Off");

    // Disable Defender notifications
    SetRegistryDWORD(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Policies\\Microsoft\\Windows Defender Security Center\\Notifications",
        "DisableNotifications", 1);

    // Remove Defender from system tray
    SetRegistryDWORD(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Policies\\Microsoft\\Windows Defender Security Center\\Systray",
        "HideSystray", 1);

    // Disable automatic sample submission
    SetRegistryDWORD(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Policies\\Microsoft\\Windows Defender\\Spynet",
        "SubmitSamplesConsent", 2);

    PrintSuccess("Windows Defender disabled");
    PrintInfo("Note: If Tamper Protection is ON, some settings require manual toggle first.");
    PrintInfo("Go to Windows Security > Virus & threat protection > Manage settings > Tamper Protection OFF");
}

// ─── Step 8: Disable Core Isolation / Device Security ───────────────────────

static void DisableCoreIsolation() {
    PrintStep("Disabling Core Isolation and Device Security...");

    // Disable Memory Integrity (HVCI)
    PrintInfo("Disabling Memory Integrity (HVCI)...");
    SetRegistryDWORD(HKEY_LOCAL_MACHINE,
        "SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\HypervisorEnforcedCodeIntegrity",
        "Enabled", 0);
    SetRegistryDWORD(HKEY_LOCAL_MACHINE,
        "SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\HypervisorEnforcedCodeIntegrity",
        "WasEnabledBy", 0);
    SetRegistryDWORD(HKEY_LOCAL_MACHINE,
        "SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\HypervisorEnforcedCodeIntegrity",
        "ChangedInBootCycle", 0);

    // Disable Credential Guard
    PrintInfo("Disabling Credential Guard...");
    SetRegistryDWORD(HKEY_LOCAL_MACHINE,
        "SYSTEM\\CurrentControlSet\\Control\\DeviceGuard", "EnableVirtualizationBasedSecurity", 0);
    SetRegistryDWORD(HKEY_LOCAL_MACHINE,
        "SYSTEM\\CurrentControlSet\\Control\\DeviceGuard", "RequirePlatformSecurityFeatures", 0);
    SetRegistryDWORD(HKEY_LOCAL_MACHINE,
        "SYSTEM\\CurrentControlSet\\Control\\Lsa", "LsaCfgFlags", 0);

    // Disable Vulnerable Driver Blocklist
    PrintInfo("Disabling Vulnerable Driver Blocklist...");
    SetRegistryDWORD(HKEY_LOCAL_MACHINE,
        "SYSTEM\\CurrentControlSet\\Control\\CI\\Config", "VulnerableDriverBlocklistEnable", 0);

    // Disable Kernel DMA Protection notification (if not hardware-supported, just suppress)
    SetRegistryDWORD(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Policies\\Microsoft\\FVE", "DisableExternalDMAUnderLock", 0);

    // Disable WDAC (Windows Defender Application Control) if configured
    SetRegistryDWORD(HKEY_LOCAL_MACHINE,
        "SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\SystemGuard",
        "Enabled", 0);

    // Disable Secure Boot enforcement from software side (hardware setting stays in BIOS)
    SetRegistryDWORD(HKEY_LOCAL_MACHINE,
        "SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State", "UEFISecureBootEnabled", 0);

    PrintSuccess("Core Isolation and Device Security features disabled");
    PrintInfo("Note: A reboot is required for these changes to take full effect.");
}

// ─── Step 9: Configure Taskbar Pins ─────────────────────────────────────────

static void ConfigureTaskbar() {
    PrintStep("Configuring taskbar pinned applications...");

    // Windows 11 taskbar pinning approach:
    // We write a custom taskbar layout via registry and StartMenu layout
    // Then use the shell verb approach for shortcuts

    // First, create shortcuts for items we want pinned
    RunPS(
        "# Create PowerShell shortcut\n"
        "$ws = New-Object -ComObject WScript.Shell; "
        "$s = $ws.CreateShortcut(\"$env:APPDATA\\Microsoft\\Windows\\Start Menu\\Programs\\Windows PowerShell.lnk\"); "
        "$s.TargetPath = 'C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe'; "
        "$s.Save(); "
        ""
        "# Create CMD shortcut\n"
        "$s2 = $ws.CreateShortcut(\"$env:APPDATA\\Microsoft\\Windows\\Start Menu\\Programs\\Command Prompt.lnk\"); "
        "$s2.TargetPath = 'C:\\Windows\\System32\\cmd.exe'; "
        "$s2.Save()"
    );

    // Windows 11 taskbar pin method via registry
    // This writes to the taskband registry which controls pinned items
    RunPS(
        "function Pin-ToTaskbar($exePath) {\n"
        "    $shell = New-Object -ComObject Shell.Application\n"
        "    $dir = $shell.Namespace((Split-Path $exePath))\n"
        "    $item = $dir.ParseName((Split-Path $exePath -Leaf))\n"
        "    # Try the verb approach\n"
        "    $verb = $item.Verbs() | Where-Object { $_.Name -match 'pin.*taskbar|Pin.*Taskbar' }\n"
        "    if ($verb) { $verb.DoIt() }\n"
        "}\n"
        "\n"
        "# For Windows 11 22H2+, we use the explorer protocol\n"
        "function Pin-Win11($appPath, $appName) {\n"
        "    $shortcutPath = \"$env:APPDATA\\Microsoft\\Windows\\Start Menu\\Programs\\$appName.lnk\"\n"
        "    if (-not (Test-Path $shortcutPath)) {\n"
        "        $ws = New-Object -ComObject WScript.Shell\n"
        "        $s = $ws.CreateShortcut($shortcutPath)\n"
        "        $s.TargetPath = $appPath\n"
        "        $s.Save()\n"
        "    }\n"
        "    # Use the syspin-style approach via SendKeys\n"
        "    # This creates the link in the taskbar pins folder\n"
        "    $pinsDir = \"$env:APPDATA\\Microsoft\\Internet Explorer\\Quick Launch\\User Pinned\\TaskBar\"\n"
        "    if (-not (Test-Path $pinsDir)) { New-Item -ItemType Directory -Path $pinsDir -Force | Out-Null }\n"
        "    Copy-Item $shortcutPath \"$pinsDir\\$appName.lnk\" -Force -ErrorAction SilentlyContinue\n"
        "}\n"
        "\n"
        "# Pin File Explorer\n"
        "Pin-Win11 'C:\\Windows\\explorer.exe' 'File Explorer'\n"
        "\n"
        "# Pin DebugView\n"
        "Pin-Win11 'C:\\Program Files\\DebugView\\Dbgview.exe' 'DebugView'\n"
        "\n"
        "# Pin PowerShell\n"
        "Pin-Win11 'C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe' 'Windows PowerShell'\n"
        "\n"
        "# Pin Command Prompt\n"
        "Pin-Win11 'C:\\Windows\\System32\\cmd.exe' 'Command Prompt'\n"
        "\n"
        "# Pin Chrome\n"
        "Pin-Win11 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe' 'Google Chrome'\n"
    );

    // Alternative approach: modify the TaskbarWinXP/Taskband registry blob
    // This is more reliable on modern Windows 11
    RunPS(
        "# Build the taskbar layout XML for provisioned machines\n"
        "$layoutXml = @'\n"
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<LayoutModificationTemplate\n"
        "  xmlns=\"http://schemas.microsoft.com/Start/2014/LayoutModification\"\n"
        "  xmlns:defaultlayout=\"http://schemas.microsoft.com/Start/2014/FullDefaultLayout\"\n"
        "  xmlns:taskbar=\"http://schemas.microsoft.com/Start/2014/TaskbarLayout\"\n"
        "  Version=\"1\">\n"
        "  <CustomTaskbarLayoutCollection PinListPlacement=\"Replace\">\n"
        "    <defaultlayout:TaskbarLayout>\n"
        "      <taskbar:TaskbarPinList>\n"
        "        <taskbar:DesktopApp DesktopApplicationLinkPath=\"%%APPDATA%%\\Microsoft\\Windows\\Start Menu\\Programs\\File Explorer.lnk\" />\n"
        "        <taskbar:DesktopApp DesktopApplicationLinkPath=\"%%ALLUSERSPROFILE%%\\Microsoft\\Windows\\Start Menu\\Programs\\DebugView.lnk\" />\n"
        "        <taskbar:DesktopApp DesktopApplicationLinkPath=\"%%APPDATA%%\\Microsoft\\Windows\\Start Menu\\Programs\\Windows PowerShell.lnk\" />\n"
        "        <taskbar:DesktopApp DesktopApplicationLinkPath=\"%%APPDATA%%\\Microsoft\\Windows\\Start Menu\\Programs\\Command Prompt.lnk\" />\n"
        "        <taskbar:DesktopApp DesktopApplicationLinkPath=\"%%ALLUSERSPROFILE%%\\Microsoft\\Windows\\Start Menu\\Programs\\Google Chrome.lnk\" />\n"
        "      </taskbar:TaskbarPinList>\n"
        "    </defaultlayout:TaskbarLayout>\n"
        "  </CustomTaskbarLayoutCollection>\n"
        "</LayoutModificationTemplate>\n"
        "'@\n"
        "\n"
        "$layoutPath = 'C:\\VMSetup_Temp\\TaskbarLayout.xml'\n"
        "$layoutXml | Out-File -FilePath $layoutPath -Encoding utf8\n"
        "\n"
        "# Import the layout\n"
        "try {\n"
        "    Import-StartLayout -LayoutPath $layoutPath -MountPath 'C:\\' -ErrorAction SilentlyContinue\n"
        "} catch {}\n"
        "\n"
        "# Also set via registry for current user\n"
        "Set-ItemProperty -Path 'HKCU:\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Taskband' -Name 'FavoritesChanges' -Value 1 -ErrorAction SilentlyContinue\n"
    );

    // Unpin default items that we don't want
    RunPS(
        "# Remove default pins we don't want (Microsoft Store, Mail, etc.)\n"
        "$pinsDir = \"$env:APPDATA\\Microsoft\\Internet Explorer\\Quick Launch\\User Pinned\\TaskBar\"\n"
        "if (Test-Path $pinsDir) {\n"
        "    Get-ChildItem $pinsDir -Filter '*.lnk' | ForEach-Object {\n"
        "        $ws = New-Object -ComObject WScript.Shell\n"
        "        $link = $ws.CreateShortcut($_.FullName)\n"
        "        $target = $link.TargetPath\n"
        "        # Keep only our desired apps\n"
        "        $keep = @('explorer.exe','Dbgview.exe','powershell.exe','cmd.exe','chrome.exe')\n"
        "        $fileName = [System.IO.Path]::GetFileName($target)\n"
        "        if ($fileName -and $keep -notcontains $fileName) {\n"
        "            Remove-Item $_.FullName -Force\n"
        "        }\n"
        "    }\n"
        "}\n"
    );

    // Restart Explorer to apply changes
    PrintInfo("Restarting Explorer to apply taskbar changes...");
    RunCommand("taskkill.exe /F /IM explorer.exe", true, 10000);
    Sleep(2000);
    RunCommand("cmd.exe /c start explorer.exe", false);

    PrintSuccess("Taskbar configured with: File Explorer, DebugView, PowerShell, CMD, Chrome");
    PrintInfo("Note: Some taskbar pins may require a reboot or re-login to fully appear.");
}

// ─── Cleanup ────────────────────────────────────────────────────────────────

static void Cleanup() {
    PrintStep("Cleaning up temporary files...");
    RunPS("Remove-Item -Path 'C:\\VMSetup_Temp' -Recurse -Force -ErrorAction SilentlyContinue");
    PrintSuccess("Cleanup complete");
}

// ─── Main ───────────────────────────────────────────────────────────────────

int main() {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    SetConsoleTextAttribute(hConsole, 14); // yellow
    printf("=============================================================\n");
    printf("                    VM Setup Tool v1.0                       \n");
    printf("         Automated VM Configuration for Debugging            \n");
    printf("=============================================================\n");
    SetConsoleTextAttribute(hConsole, 7);

    // Check for admin privileges
    BOOL isAdmin = FALSE;
    SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
    PSID adminGroup = NULL;
    if (AllocateAndInitializeSid(&ntAuth, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                  DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }

    if (!isAdmin) {
        SetConsoleTextAttribute(hConsole, 12);
        printf("\n[!] This tool must be run as Administrator!\n");
        printf("    Right-click the exe and select 'Run as administrator'.\n");
        SetConsoleTextAttribute(hConsole, 7);
        printf("\nPress Enter to exit...");
        getchar();
        return 1;
    }

    printf("\nThis tool will:\n");
    printf("  1. Install runtimes (VC++, .NET 6/8)\n");
    printf("  2. Remove Microsoft Edge\n");
    printf("  3. Install Google Chrome\n");
    printf("  4. Install DebugView (Sysinternals)\n");
    printf("  5. Set desktop background to grey\n");
    printf("  6. Apply performance optimizations\n");
    printf("  7. Disable Windows Defender\n");
    printf("  8. Disable Core Isolation / Device Security\n");
    printf("  9. Configure taskbar pins\n");
    printf("\n");

    SetConsoleTextAttribute(hConsole, 14);
    printf("Press Enter to begin, or close this window to cancel...");
    SetConsoleTextAttribute(hConsole, 7);
    getchar();

    // Execute all steps in order
    InstallRuntimes();
    RemoveEdge();
    InstallChrome();
    InstallDebugView();
    SetGreyBackground();
    OptimizePerformance();
    DisableDefender();
    DisableCoreIsolation();
    ConfigureTaskbar();
    Cleanup();

    // Final summary
    printf("\n");
    SetConsoleTextAttribute(hConsole, 10);
    printf("=============================================================\n");
    printf("                  VM Setup Complete!                          \n");
    printf("=============================================================\n");
    SetConsoleTextAttribute(hConsole, 7);
    printf("\nIMPORTANT: A reboot is strongly recommended for all changes\n");
    printf("to take full effect (especially Defender and Core Isolation).\n");
    printf("\nIf Tamper Protection was ON, you may need to:\n");
    printf("  1. Reboot\n");
    printf("  2. Open Windows Security\n");
    printf("  3. Turn off Tamper Protection manually\n");
    printf("  4. Run this tool again\n");

    printf("\nPress Enter to exit...");
    getchar();
    return 0;
}
