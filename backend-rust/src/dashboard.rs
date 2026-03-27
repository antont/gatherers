use crate::summary::CachedSnapshot;

pub fn render_dashboard(snapshot: &CachedSnapshot) -> String {
    format!(
        r#"<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8">
    <title>Gatherers Backend</title>
  </head>
  <body>
    <h1>Connected sims</h1>
    <div id="connected-sims-value">{connected_sims}</div>
    <div id="loose-food-value">{loose_food}</div>
    <div id="elapsed-seconds-value">{elapsed:.2}</div>
    <div id="events-per-second-value">{eps:.2}</div>
    <div id="occupied-cells-value">{occupied_cells}</div>
    <div id="nearest-neighbor-value">{nearest_neighbor:.2}</div>
    <div id="analytics-age-value">{analytics_age:.2}</div>
    <table>
      <thead>
        <tr><th>Sim</th><th>Ants</th></tr>
      </thead>
      <tbody id="sim-table-body"></tbody>
    </table>
    <script>
      window.GATHERERS_DASHBOARD_WS_URL = "/ws/dashboard";
      const ignoredExample = "sim.ant_count";
    </script>
  </body>
</html>"#,
        connected_sims = snapshot.summary.live_summary.connected_sim_count,
        loose_food = snapshot.summary.live_summary.loose_food_count,
        elapsed = snapshot.summary.live_summary.elapsed_seconds,
        eps = snapshot.summary.live_summary.events_per_second,
        occupied_cells = snapshot.summary.analytics_summary.occupied_cell_count,
        nearest_neighbor = snapshot.summary.analytics_summary.nearest_neighbor_mean_distance,
        analytics_age = snapshot.summary.analytics_meta.age_seconds,
    )
}
