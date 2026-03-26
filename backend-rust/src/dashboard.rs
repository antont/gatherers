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
    <div id="connected-sims-value">{}</div>
    <div id="loose-food-value">{}</div>
    <div id="elapsed-seconds-value">{:.2}</div>
    <div id="events-per-second-value">{:.2}</div>
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
        snapshot.summary.connected_sim_count,
        snapshot.summary.loose_food_count,
        snapshot.summary.elapsed_seconds,
        snapshot.summary.events_per_second,
    )
}
