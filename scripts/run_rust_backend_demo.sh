#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

count=9
backend_addr="127.0.0.1:18080"
speed="10"
seed_base=1
sim_id_prefix="demo"
grid="auto"
window_width=420
window_height=260
open_dashboard=0

listener_pid_for_port() {
  local port="$1"
  lsof -tiTCP:"${port}" -sTCP:LISTEN 2>/dev/null | python3 -c 'import sys; lines=[line.strip() for line in sys.stdin if line.strip()]; print(lines[0] if lines else "")'
}

listener_command_for_pid() {
  local pid="$1"
  ps -p "${pid}" -o command= 2>/dev/null || true
}

wait_for_port_release() {
  local port="$1"
  for _ in $(seq 1 50); do
    if [[ -z "$(listener_pid_for_port "${port}")" ]]; then
      return 0
    fi
    sleep 0.1
  done
  return 1
}

choose_backend_addr() {
  local requested_addr="$1"
  local host="${requested_addr%:*}"
  local port="${requested_addr##*:}"
  local pid
  local command

  pid="$(listener_pid_for_port "${port}")"
  if [[ -z "${pid}" ]]; then
    echo "${requested_addr}"
    return 0
  fi

  command="$(listener_command_for_pid "${pid}")"
  if [[ "${command}" == *"gatherers-backend-rust"* ]]; then
    echo "Stopping stale Rust backend on ${requested_addr} (pid ${pid})" >&2
    kill "${pid}" 2>/dev/null || true
    if ! wait_for_port_release "${port}"; then
      echo "error: stale Rust backend on ${requested_addr} did not exit cleanly" >&2
      exit 1
    fi
    echo "${requested_addr}"
    return 0
  fi

  for candidate_port in $(seq "$((port + 1))" "$((port + 20))"); do
    if [[ -z "$(listener_pid_for_port "${candidate_port}")" ]]; then
      local candidate_addr="${host}:${candidate_port}"
      echo "Port ${requested_addr} is busy (${command}); switching demo backend to ${candidate_addr}" >&2
      echo "${candidate_addr}"
      return 0
    fi
  done

  echo "error: ${requested_addr} is busy (${command}) and no fallback port was found" >&2
  exit 1
}

usage() {
  cat <<'EOF'
Usage: scripts/run_rust_backend_demo.sh [options]

Options:
  --count N               Number of Bevy sims to launch (default: 9)
  --backend-addr ADDR     Rust backend bind address (default: 127.0.0.1:18080)
  --speed VALUE           Startup speed multiplier for each sim (default: 10)
  --seed-base N           Base seed; each sim uses seed_base + index (default: 1)
  --sim-id-prefix PREFIX  Prefix for generated sim ids (default: demo)
  --grid auto|CxR         Window grid, or auto for square-ish tiling (default: auto)
  --window-size WxH       Window size for each sim (default: 420x260)
  --open-dashboard        Open the dashboard in the default browser on macOS
  --help                  Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --count)
      count="$2"
      shift 2
      ;;
    --backend-addr)
      backend_addr="$2"
      shift 2
      ;;
    --speed)
      speed="$2"
      shift 2
      ;;
    --seed-base)
      seed_base="$2"
      shift 2
      ;;
    --sim-id-prefix)
      sim_id_prefix="$2"
      shift 2
      ;;
    --grid)
      grid="$2"
      shift 2
      ;;
    --window-size)
      window_size="$2"
      if [[ ! "$window_size" =~ ^([0-9]+)x([0-9]+)$ ]]; then
        echo "error: --window-size must look like 420x260" >&2
        exit 1
      fi
      window_width="${BASH_REMATCH[1]}"
      window_height="${BASH_REMATCH[2]}"
      shift 2
      ;;
    --open-dashboard)
      open_dashboard=1
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

if ! [[ "$count" =~ ^[0-9]+$ ]] || [[ "$count" -lt 1 ]]; then
  echo "error: --count must be a positive integer" >&2
  exit 1
fi

if ! [[ "$seed_base" =~ ^[0-9]+$ ]]; then
  echo "error: --seed-base must be a non-negative integer" >&2
  exit 1
fi

cols=0
rows=0
if [[ "$grid" == "auto" ]]; then
  cols=$(python3 - <<PY
import math
count = $count
print(max(1, math.ceil(math.sqrt(count))))
PY
)
  rows=$(( (count + cols - 1) / cols ))
else
  if [[ ! "$grid" =~ ^([0-9]+)x([0-9]+)$ ]]; then
    echo "error: --grid must be auto or look like 3x3" >&2
    exit 1
  fi
  cols="${BASH_REMATCH[1]}"
  rows="${BASH_REMATCH[2]}"
fi

backend_addr="$(choose_backend_addr "${backend_addr}")"
dashboard_url="http://${backend_addr}/"
backend_ws_url="ws://${backend_addr}/ws/ingest"
sim_binary="${repo_root}/target/debug/an-gatherers"
backend_binary="${repo_root}/backend-rust/target/debug/gatherers-backend-rust"
if [[ -n "${GATHERERS_DEMO_SIM_BIN:-}" ]]; then
  sim_binary="${GATHERERS_DEMO_SIM_BIN}"
fi
if [[ -n "${GATHERERS_DEMO_BACKEND_BIN:-}" ]]; then
  backend_binary="${GATHERERS_DEMO_BACKEND_BIN}"
fi

declare -a child_pids=()

terminate_child() {
  local pid="$1"
  if ! kill -0 "${pid}" 2>/dev/null; then
    return 0
  fi

  kill "${pid}" 2>/dev/null || true
  for _ in $(seq 1 30); do
    if ! kill -0 "${pid}" 2>/dev/null; then
      return 0
    fi
    sleep 0.1
  done

  kill -KILL "${pid}" 2>/dev/null || true
}

cleanup() {
  local exit_code=$?
  trap - EXIT INT TERM
  for pid in "${child_pids[@]:-}"; do
    terminate_child "${pid}"
  done
  wait 2>/dev/null || true
  exit "$exit_code"
}
trap cleanup EXIT INT TERM

if [[ "${GATHERERS_DEMO_SKIP_BUILD:-0}" != "1" ]]; then
  echo "Building Bevy sim and Rust backend binaries..."
  cargo build --manifest-path "${repo_root}/Cargo.toml" --bin an-gatherers
  cargo build --manifest-path "${repo_root}/backend-rust/Cargo.toml" --bin gatherers-backend-rust
fi

echo "Starting Rust backend on ${backend_addr}"
(
  cd "${repo_root}/backend-rust"
  exec env GATHERERS_BACKEND_RUST_ADDR="${backend_addr}" "${backend_binary}"
) &
child_pids+=("$!")

echo "Waiting for backend health check..."
for _ in $(seq 1 100); do
  if curl -fsS "${dashboard_url}healthz" >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done

if ! curl -fsS "${dashboard_url}healthz" >/dev/null 2>&1; then
  echo "error: backend did not become healthy at ${dashboard_url}healthz" >&2
  exit 1
fi

if ! curl -fsS "${dashboard_url}dashboard.css" >/dev/null 2>&1; then
  echo "error: backend at ${dashboard_url} did not expose /dashboard.css; refusing to launch sims against the wrong server" >&2
  exit 1
fi

echo "Dashboard: ${dashboard_url}"
if [[ "$open_dashboard" -eq 1 ]] && command -v open >/dev/null 2>&1; then
  open "${dashboard_url}"
fi

for ((index = 0; index < count; index++)); do
  row=$(( index / cols ))
  col=$(( index % cols ))
  x=$(( 20 + col * window_width ))
  y=$(( 60 + row * window_height ))
  seed=$(( seed_base + index ))
  sim_id="$(printf "%s-%02d" "${sim_id_prefix}" "$((index + 1))")"
  title="gatherers ${sim_id}"

  echo "Launching ${sim_id} seed=${seed} at (${x},${y})"
  (
    cd "${repo_root}"
    exec env \
      GATHERERS_BACKEND_WS_URL="${backend_ws_url}" \
      GATHERERS_SIM_ID="${sim_id}" \
      GATHERERS_SIM_SEED="${seed}" \
      GATHERERS_STARTUP_SPEED="${speed}" \
      GATHERERS_WINDOW_TITLE="${title}" \
      GATHERERS_WINDOW_X="${x}" \
      GATHERERS_WINDOW_Y="${y}" \
      GATHERERS_WINDOW_WIDTH="${window_width}" \
      GATHERERS_WINDOW_HEIGHT="${window_height}" \
      "${sim_binary}"
  ) &
  child_pids+=("$!")
done

echo "Spawned ${count} Bevy sims. Press Ctrl-C to stop them and the backend."
wait
