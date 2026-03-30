#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
project_path="${repo_root}/unreal_gatherers/unreal_gatherers.uproject"
engine_root="${UNREAL_ENGINE_ROOT:-/Users/Shared/Epic Games/UE_5.7}"
build_tool="${engine_root}/Engine/Build/BatchFiles/Mac/Build.sh"
editor_binary="${engine_root}/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"

test_filter="unreal_gatherers"
should_build=1

usage() {
  cat <<'EOF'
Usage: scripts/test_unreal.sh [options]

Runs the Unreal automation suite for this project.

Options:
  --test FILTER   Automation filter to run (default: unreal_gatherers)
  --no-build      Skip the Unreal editor target build step
  --help          Show this help

Environment:
  UNREAL_ENGINE_ROOT  Unreal installation root
                      (default: /Users/Shared/Epic Games/UE_5.7)
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --test)
      test_filter="$2"
      shift 2
      ;;
    --no-build)
      should_build=0
      shift
      ;;
    --help)
      usage
      exit 0
      ;;
    *)
      echo "error: unknown option $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [[ ! -f "${project_path}" ]]; then
  echo "error: Unreal project not found at ${project_path}" >&2
  exit 1
fi

if [[ ! -x "${editor_binary}" ]]; then
  echo "error: UnrealEditor not found at ${editor_binary}" >&2
  exit 1
fi

if [[ "${should_build}" -eq 1 ]]; then
  if [[ ! -x "${build_tool}" ]]; then
    echo "error: Unreal build tool not found at ${build_tool}" >&2
    exit 1
  fi

  echo "Building unreal_gatherersEditor..."
  "${build_tool}" unreal_gatherersEditor Mac Development "-Project=${project_path}"
fi

echo "Running Unreal automation filter: ${test_filter}"
if "${editor_binary}" \
  "${project_path}" \
  "-ExecCmds=Automation RunTest ${test_filter};Quit" \
  -unattended -nop4 -nosplash -NullRHI -stdout -FullStdOutLogOutput; then
  echo "PASS: Unreal automation filter '${test_filter}'"
else
  status=$?
  echo "FAIL: Unreal automation filter '${test_filter}'" >&2
  exit "${status}"
fi
