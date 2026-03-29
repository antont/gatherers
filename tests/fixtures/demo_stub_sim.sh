#!/usr/bin/env bash
set -euo pipefail

log_dir="${GATHERERS_DEMO_LOG_DIR:?GATHERERS_DEMO_LOG_DIR is required}"
sim_id="${GATHERERS_SIM_ID:?GATHERERS_SIM_ID is required}"

mkdir -p "${log_dir}"
printf '%s\n' "$$" > "${log_dir}/sim-${sim_id}.pid"

cleanup() {
  exit 0
}
trap cleanup INT TERM

while true; do
  sleep 1
done
