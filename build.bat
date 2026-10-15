@echo off
setlocal
cd /d "%~dp0"

echo ===================================================
echo   MapFix: Deadly Premonition Enhanced Map Mod
echo   Building 32-bit ASI plugin
echo ===================================================

if not exist "build" mkdir "build"

set "ZIG_EXE="
where zig >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    set "ZIG_EXE=zig"
) else if exist "%USERPROFILE%\scoop\apps\zig\current\zig.exe" (
    set "ZIG_EXE=%USERPROFILE%\scoop\apps\zig\current\zig.exe"
) else if exist "%USERPROFILE%\scoop\shims\zig.exe" (
    set "ZIG_EXE=%USERPROFILE%\scoop\shims\zig.exe"
)

if defined ZIG_EXE (
    echo [*] Compiling with Zig: %ZIG_EXE%
    "%ZIG_EXE%" cc -target x86-windows -O2 -shared src\MapFix.c -o build\MapFix.asi
    if errorlevel 1 (
        echo [!] Compilation failed
        exit /b 1
    )
) else (
    echo [!] Zig compiler not found. Please install Zig or run via MSVC Developer Command Prompt.
    exit /b 1
)

echo [*] Build succeeded: build\MapFix.asi

if exist "..\scripts" (
    echo [*] Deploying to scripts folder
    copy /y "build\MapFix.asi" "..\scripts\MapFix.asi" >nul 2>&1
    if not exist "..\scripts\MapFix.ini" (
        copy /y "config\MapFix.ini" "..\scripts\MapFix.ini" >nul 2>&1
        echo [*] Deployed default config to scripts folder
    )
    echo [*] Deployment complete
)

echo ===================================================
echo   Done!
echo ===================================================
