use serde::Serialize;

use crate::summary::{CachedSnapshot, SimSummaryResponse};

const DASHBOARD_HTML: &str =
    include_str!(concat!(env!("CARGO_MANIFEST_DIR"), "/../dashboard/index.html"));
const DASHBOARD_CSS: &str =
    include_str!(concat!(env!("CARGO_MANIFEST_DIR"), "/../dashboard/dashboard.css"));
const DASHBOARD_JS: &str =
    include_str!(concat!(env!("CARGO_MANIFEST_DIR"), "/../dashboard/dashboard.js"));

#[derive(Clone, Debug, PartialEq, Serialize)]
pub struct DashboardSnapshotView {
    pub summary: DashboardSummaryView,
    pub sims: Vec<SimSummaryResponse>,
}

#[derive(Clone, Debug, PartialEq, Serialize)]
pub struct DashboardSummaryView {
    pub connected_sim_count: usize,
    pub loose_food_count: usize,
    pub occupied_cell_count: usize,
    pub nearest_neighbor_mean_distance: f64,
    pub elapsed_seconds: f64,
    pub events_per_second: f64,
    pub analytics_age_seconds: f64,
    pub analytics_is_stale: bool,
    pub analytics_computed_at_ms: i64,
}

pub fn render_dashboard() -> &'static str {
    DASHBOARD_HTML
}

pub fn dashboard_css() -> &'static str {
    DASHBOARD_CSS
}

pub fn dashboard_js() -> &'static str {
    DASHBOARD_JS
}

pub fn normalize_snapshot(snapshot: &CachedSnapshot) -> DashboardSnapshotView {
    DashboardSnapshotView {
        summary: DashboardSummaryView {
            connected_sim_count: snapshot.summary.live_summary.connected_sim_count,
            loose_food_count: snapshot.summary.live_summary.loose_food_count,
            occupied_cell_count: snapshot.summary.analytics_summary.occupied_cell_count,
            nearest_neighbor_mean_distance: snapshot
                .summary
                .analytics_summary
                .nearest_neighbor_mean_distance,
            elapsed_seconds: snapshot.summary.live_summary.elapsed_seconds,
            events_per_second: snapshot.summary.live_summary.events_per_second,
            analytics_age_seconds: snapshot.summary.analytics_meta.age_seconds,
            analytics_is_stale: snapshot.summary.analytics_meta.is_stale,
            analytics_computed_at_ms: snapshot.summary.analytics_meta.computed_at_ms,
        },
        sims: snapshot.sims.clone(),
    }
}
