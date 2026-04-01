#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

exec "${repo_root}/scripts/test_unreal.sh" \
  "$@" \
  --test supplemental.unreal_gatherers.TimeControl.MaxCorrectSpeedSweepReportsHighestPassingDilation
