const connectedSimsValue = document.getElementById("connected-sims-value");
const looseFoodValue = document.getElementById("loose-food-value");
const occupiedCellsValue = document.getElementById("occupied-cells-value");
const nearestNeighborValue = document.getElementById("nearest-neighbor-value");
const elapsedSecondsValue = document.getElementById("elapsed-seconds-value");
const eventsPerSecondValue = document.getElementById("events-per-second-value");
const lastUpdatedValue = document.getElementById("last-updated-value");
const simTableBody = document.getElementById("sim-table-body");

function render(snapshot) {
  connectedSimsValue.textContent = String(snapshot.summary.connected_sim_count ?? 0);
  looseFoodValue.textContent = String(snapshot.summary.loose_food_count ?? 0);
  occupiedCellsValue.textContent = String(snapshot.summary.occupied_cell_count ?? 0);
  nearestNeighborValue.textContent = Number(snapshot.summary.nearest_neighbor_mean_distance ?? 0).toFixed(2);
  elapsedSecondsValue.textContent = Number(snapshot.summary.elapsed_seconds ?? 0).toFixed(1) + "s";
  eventsPerSecondValue.textContent = Number(snapshot.summary.events_per_second ?? 0).toFixed(2);

  if (snapshot.summary.analytics_is_stale) {
    lastUpdatedValue.textContent = "analytics stale; live counts still updating";
  } else {
    lastUpdatedValue.textContent = new Date().toLocaleTimeString();
  }

  const sims = [...(snapshot.sims ?? [])].sort((a, b) => a.sim_id.localeCompare(b.sim_id));
  if (sims.length === 0) {
    simTableBody.innerHTML = '<tr><td colspan="6">No sims connected yet.</td></tr>';
    return;
  }

  simTableBody.innerHTML = sims.map((sim) => {
    return '<tr>' +
      '<td>' + sim.sim_id + '</td>' +
      '<td>' + (sim.ant_count ?? 0) + '</td>' +
      '<td>' + (sim.drop_count ?? 0) + '</td>' +
      '<td>' + (sim.pickup_count ?? 0) + '</td>' +
      '<td>' + (sim.turn_move_count ?? 0) + '</td>' +
      '<td>' + (sim.loose_food_count ?? 0) + '</td>' +
      '</tr>';
  }).join('');
}

const scheme = window.location.protocol === "https:" ? "wss" : "ws";
const socket = new WebSocket(scheme + "://" + window.location.host + "/ws/dashboard");
socket.addEventListener("message", (event) => {
  render(JSON.parse(event.data));
});
socket.addEventListener("close", () => {
  lastUpdatedValue.textContent = "dashboard websocket disconnected";
});
