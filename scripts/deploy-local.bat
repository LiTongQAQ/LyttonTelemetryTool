@echo off
rem Copies the freshly built program (build-qt\app, plus the old window as LyttonClassic.exe) into the installed
rem program (%LOCALAPPDATA%\Programs\Lytton Telemetry Tool). Double-click after a build; the program must be closed.
chcp 65001 >nul
setlocal
set "SRC=%~dp0..\build-qt\app"
set "OLD=%~dp0..\build\f1-race.exe"
set "DST=%LOCALAPPDATA%\Programs\Lytton Telemetry Tool"

if not exist "%SRC%\LyttonTelemetryTool.exe" (
    echo 找不到 %SRC%\LyttonTelemetryTool.exe，先编译。
    goto end
)
if not exist "%DST%\" (
    echo 找不到安装目录 %DST%，先用安装包装一次。
    goto end
)

:check
set RUNNING=
tasklist /FI "IMAGENAME eq LyttonTelemetryTool.exe" 2>nul | find /I "LyttonTelemetryTool.exe" >nul && set RUNNING=1
tasklist /FI "IMAGENAME eq LyttonClassic.exe" 2>nul | find /I "LyttonClassic.exe" >nul && set RUNNING=1
if defined RUNNING (
    echo 程序正在运行，请先关掉它，然后按任意键继续……
    pause >nul
    goto check
)

robocopy "%SRC%" "%DST%" /E /XF *.ini *.log .lytton-write-test /XD recordings /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 (
    echo 复制失败。
    goto end
)
if exist "%OLD%" copy /Y "%OLD%" "%DST%\LyttonClassic.exe" >nul
if exist "%~dp0..\LICENSE" copy /Y "%~dp0..\LICENSE" "%DST%\LICENSE" >nul
for %%F in ("%DST%\LyttonTelemetryTool.exe") do echo 已部署：%%~fF  （%%~tF，%%~zF 字节）

:end
echo.
pause
