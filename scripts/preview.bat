@echo off
rem Preview the real UI after editing the QML in Design Studio: rebuilds the Qt program (QML is compiled into the
rem exe) and starts it with the virtual race. Optional argument: the page to open (0 Live Dash .. 7 Settings).
chcp 65001 >nul
setlocal
set "ROOT=%~dp0.."
set "PATH=C:\Qt\Tools\mingw1310_64\bin;D:\YzcWorkSpace\Qt\Tools\CMake_64\bin;%PATH%"
set "PAGE=%~1"
if "%PAGE%"=="" set "PAGE=0"

tasklist /FI "IMAGENAME eq LyttonTelemetryTool.exe" 2>nul | find /I "LyttonTelemetryTool.exe" >nul
if not errorlevel 1 (
    echo LyttonTelemetryTool 正在运行，先关掉它再预览。
    pause
    goto end
)

echo 正在编译……
cmake --build "%ROOT%\build-qt" -j 8 --target lytton >"%ROOT%\build-qt\preview-build.log" 2>&1
if errorlevel 1 (
    echo 编译失败，详情见 build-qt\preview-build.log：
    findstr /C:"error" "%ROOT%\build-qt\preview-build.log"
    pause
    goto end
)
start "" "%ROOT%\build-qt\app\LyttonTelemetryTool.exe" --demo --page %PAGE%
echo 已启动虚拟比赛预览（第 %PAGE% 页）。QML 有问题时看 build-qt\app\lytton.log 里 "qt warning" 开头的行。

:end
endlocal
