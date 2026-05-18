@echo off
echo ============================================
echo  Building VMSetup.exe (static, no runtime)
echo ============================================
echo.

:: Try to find Visual Studio's cl.exe
set "FOUND_CL="

:: VS 2022 Community/Professional/Enterprise
for %%p in (
    "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
) do (
    if exist %%p (
        echo Found: %%p
        call %%p
        set "FOUND_CL=1"
        goto :build
    )
)

:: Check if cl.exe is already in PATH (e.g., Developer Command Prompt)
where cl.exe >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    set "FOUND_CL=1"
    goto :build
)

if not defined FOUND_CL (
    echo.
    echo ERROR: Could not find Visual Studio or Build Tools.
    echo.
    echo Please install one of the following:
    echo   - Visual Studio 2022 with "Desktop development with C++" workload
    echo   - Visual Studio Build Tools 2022 (free) from:
    echo     https://aka.ms/vs/17/release/vs_BuildTools.exe
    echo.
    echo Or run this script from a "Developer Command Prompt for VS"
    echo.
    pause
    exit /b 1
)

:build
echo.
echo Compiling VMSetup.cpp...
echo.

cl.exe /nologo /O2 /MT /EHsc /DUNICODE /D_UNICODE /DWIN32 /D_WINDOWS ^
    /Fe:VMSetup.exe ^
    VMSetup.cpp ^
    /link /SUBSYSTEM:CONSOLE ^
    wininet.lib shell32.lib user32.lib advapi32.lib ole32.lib gdi32.lib ^
    /MANIFEST:EMBED ^
    /MANIFESTUAC:"level='requireAdministrator' uiAccess='false'"

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Build FAILED.
    pause
    exit /b 1
)

echo.
echo ============================================
echo  Build successful: VMSetup.exe
echo ============================================
echo.
echo The exe will auto-request admin elevation when run.
echo No runtime dependencies required.
echo.

:: Clean up intermediate files
del /q VMSetup.obj 2>nul

pause
