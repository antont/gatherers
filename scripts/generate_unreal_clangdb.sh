#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
project_root="${repo_root}/unreal_gatherers"
project_path="${project_root}/unreal_gatherers.uproject"
engine_root="${UNREAL_ENGINE_ROOT:-/Users/Shared/Epic Games/UE_5.7}"
dotnet_bin="${engine_root}/Engine/Binaries/ThirdParty/DotNet/8.0.412/mac-arm64/dotnet"
ubt_dll="${engine_root}/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll"

if [[ ! -f "${project_path}" ]]; then
  echo "error: Unreal project not found at ${project_path}" >&2
  exit 1
fi

if [[ ! -x "${dotnet_bin}" ]]; then
  echo "error: dotnet runtime not found at ${dotnet_bin}" >&2
  exit 1
fi

if [[ ! -f "${ubt_dll}" ]]; then
  echo "error: UnrealBuildTool not found at ${ubt_dll}" >&2
  exit 1
fi

"${dotnet_bin}" "${ubt_dll}" \
  -Mode=GenerateClangDatabase \
  unreal_gatherersEditor Mac Development \
  "-Project=${project_path}" \
  "-OutputDir=${project_root}" \
  -OutputFilename=compile_commands.json

echo
echo "Wrote ${project_root}/compile_commands.json"
echo "If Cursor does not refresh automatically, reload the window."
