@echo off
rem moonlight-mic — Windows build script for client-pc
rem Produces a standalone Moonlight.exe deployment in the local build dir.
rem
rem Usage: run this bat from any directory; it locates its own source root.
rem       The output lands in <build-dir>\moonlight-qt-x64-release\
rem
rem Requirements:
rem   - Qt 6.10.3 msvc2022_64 installed at C:\Qt\6.10.3\msvc2022_64\
rem   - VS 2022 Community with C++ workload
rem
rem DLL notes (Q2, 2026-05-03):
rem   Stock Moonlight installer (6.1.0, Sept 2024) ships:
rem     avcodec-61.dll, avutil-59.dll, libplacebo-349.dll, swscale-8.dll
rem   This build ships newer upstream FFmpeg/libplacebo versions:
rem     avcodec-62.dll, avutil-60.dll, libplacebo-360.dll, swscale-9.dll
rem   dav1d.dll is present in both — AV1 decode works via D3D11VA hardware
rem   acceleration on this machine (AMD Ryzen 5 6600H, Rembrandt RDNA2 iGPU).
rem   The AV1 negotiation failure observed 2026-05-02 was on the Apollo host
rem   side (our patched Apollo was not advertising AV1 support), not a missing
rem   decoder library on the client side.
rem   icuuc.dll: Qt's windeployqt copies it for ARM64 cross-builds; upstream
rem   build-arch.bat deletes it for x64 (see line 238 in scripts\build-arch.bat).
rem   We follow the same behaviour and delete it after windeployqt.

setlocal enabledelayedexpansion

rem Locate script/source root regardless of where the script is run from.
rem %~dp0 resolves to the scripts\ directory; go one level up for the repo root.
set SCRIPT_DIR=%~dp0
set SOURCE_ROOT=%SCRIPT_DIR:~0,-1%
for %%i in ("%SOURCE_ROOT%\..") do set SOURCE_ROOT=%%~fi

echo Source root: %SOURCE_ROOT%

set QT_PATH=C:\Qt\6.10.3\msvc2022_64\bin
set PATH=%QT_PATH%;%PATH%

echo Calling vcvarsall.bat for x64...
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 (
    echo vcvarsall.bat failed
    exit /b 1
)

rem Build output dir. Override by setting MOONLIGHT_MIC_BUILD_ROOT in the environment.
if not defined MOONLIGHT_MIC_BUILD_ROOT set MOONLIGHT_MIC_BUILD_ROOT=C:\moonlight-mic-build
set BUILD_DIR=%MOONLIGHT_MIC_BUILD_ROOT%\moonlight-qt-x64-release
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

rem Optional debug-mic-ab-capture flag (A1, 2026-05-03):
rem   set DEBUG_MIC_AB_CAPTURE=1 (any non-empty value) before invoking this
rem   script to build with the debug A/B capture instrumentation enabled.
set EXTRA_QMAKE_CONFIG=
if defined DEBUG_MIC_AB_CAPTURE (
    if not "%DEBUG_MIC_AB_CAPTURE%"=="" (
        set EXTRA_QMAKE_CONFIG=CONFIG+=debug-mic-ab-capture
        echo Debug feature ENABLED: DEBUG_MIC_AB_CAPTURE
    )
)

echo.
echo qmake configure...
pushd "%BUILD_DIR%"
qmake "%SOURCE_ROOT%\moonlight-qt.pro" CONFIG+=release %EXTRA_QMAKE_CONFIG%
if errorlevel 1 (
    echo qmake failed
    popd
    exit /b 1
)

echo.
echo jom build (parallel)...
"%SOURCE_ROOT%\scripts\jom.exe" release
set RC=%errorlevel%
popd

if %RC% NEQ 0 (
    echo jom failed with code %RC%
    exit /b %RC%
)

echo.
echo Deploying Qt DLLs alongside Moonlight.exe...
set DEPLOY_DIR=%BUILD_DIR%\app\release
"%QT_PATH%\windeployqt.exe" --release --no-opengl-sw --no-compiler-runtime --no-sql --no-system-d3d-compiler --no-system-dxc-compiler --skip-plugin-types qmltooling,generic --no-ffmpeg --no-quickcontrols2fusion --no-quickcontrols2imagine --no-quickcontrols2universal --no-quickcontrols2fusionstyleimpl --no-quickcontrols2imaginestyleimpl --no-quickcontrols2universalstyleimpl --no-quickcontrols2windowsstyleimpl --no-quickcontrols2fluentwinui3styleimpl --qmldir "%SOURCE_ROOT%\app\gui" "%DEPLOY_DIR%\Moonlight.exe"
if errorlevel 1 (
    echo windeployqt failed
    exit /b 1
)

rem Qt's windeployqt incorrectly deploys icuuc.dll on x64 builds;
rem it is not needed on this platform and is absent from the stock installer.
rem See upstream scripts\build-arch.bat line 238.
if exist "%DEPLOY_DIR%\icuuc.dll" (
    echo Removing icuuc.dll (not needed on x64, absent from stock installer^)...
    del /f /q "%DEPLOY_DIR%\icuuc.dll"
)

echo Copying third-party DLLs from libs\windows\lib\x64\...
copy /Y "%SOURCE_ROOT%\libs\windows\lib\x64\*.dll" "%DEPLOY_DIR%\" >nul

echo Copying AntiHooking.dll...
copy /Y "%BUILD_DIR%\AntiHooking\release\AntiHooking.dll" "%DEPLOY_DIR%\" >nul

echo Copying gamecontroller DB...
copy /Y "%SOURCE_ROOT%\app\SDL_GameControllerDB\gamecontrollerdb.txt" "%DEPLOY_DIR%\" >nul

echo.
echo BUILD SUCCESSFUL
echo Moonlight.exe deployed to: %DEPLOY_DIR%\Moonlight.exe
exit /b 0
