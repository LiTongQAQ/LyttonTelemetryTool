#!/usr/bin/env bash
# Builds the Windows installer dist/LyttonTelemetryTool-Setup-<version>.exe with Inno Setup 6 (installer/lytton.iss).
# Run after both Release builds (see CLAUDE.md):  scripts/build-installer.sh
#   build-qt/app/       the Qt program with its Qt DLLs (GCC 13.1 build; windeployqt runs after linking)
#   build/f1-race.exe   the old ImGui window, shipped as LyttonClassic.exe
# Inno Setup elsewhere: ISCC=<path>/ISCC.exe bash scripts/build-installer.sh
set -euo pipefail
cd "$(dirname "$0")/.."

ISCC="${ISCC:-/c/Program Files (x86)/Inno Setup 6/ISCC.exe}"
version=$(sed -nE 's/^project\(F1_25_Mods4Lytton VERSION ([0-9.]+).*/\1/p' CMakeLists.txt)$(sed -nE 's/^set\(F1RACE_VERSION_SUFFIX "([^"]*)"\).*/\1/p' CMakeLists.txt)
for f in build-qt/app/LyttonTelemetryTool.exe build-qt/app/openvr_api.dll build/f1-race.exe; do
  [ -f "$f" ] || { echo "missing $f: build first" >&2; exit 1; }
done
mkdir -p dist
out="dist/LyttonTelemetryTool-Setup-$version.exe"
rm -f "$out"
"$ISCC" //Q "//DAppVersion=$version" installer/lytton.iss
ls -la "$out"
