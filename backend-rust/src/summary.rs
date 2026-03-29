use serde::Serialize;

#[derive(Clone, Debug, Default, PartialEq, Serialize)]
pub struct LiveSummaryResponse {
    pub connected_sim_count: usize,
    pub loose_food_count: usize,
    pub elapsed_seconds: f64,
    pub events_per_second: f64,
}

#[derive(Clone, Debug, Default, PartialEq, Serialize)]
pub struct AnalyticsSummaryResponse {
    pub occupied_cell_count: usize,
    pub nearest_neighbor_mean_distance: f64,
}

#[derive(Clone, Debug, PartialEq, Serialize)]
pub struct AnalyticsMetaResponse {
    pub computed_at_ms: i64,
    pub age_seconds: f64,
    pub is_stale: bool,
}

impl Default for AnalyticsMetaResponse {
    fn default() -> Self {
        Self {
            computed_at_ms: 0,
            age_seconds: 0.0,
            is_stale: true,
        }
    }
}

#[derive(Clone, Debug, Default, PartialEq, Serialize)]
pub struct SummaryResponse {
    pub live_summary: LiveSummaryResponse,
    pub analytics_summary: AnalyticsSummaryResponse,
    pub analytics_meta: AnalyticsMetaResponse,
}

#[derive(Clone, Debug, Default, PartialEq, Serialize)]
pub struct SimSummaryResponse {
    pub sim_id: String,
    pub ant_count: usize,
    pub pickup_count: usize,
    pub drop_count: usize,
    pub turn_move_count: usize,
    pub loose_food_count: usize,
}

#[derive(Clone, Debug, Default, PartialEq, Serialize)]
pub struct BreakpointTotalsResponse {
    pub connected_sims: usize,
    pub pickup_count: usize,
    pub drop_count: usize,
    pub turn_move_count: usize,
    pub loose_food_count: usize,
}

#[derive(Clone, Debug, Default, PartialEq, Serialize)]
pub struct CachedLiveSnapshot {
    pub live_summary: LiveSummaryResponse,
    pub sims: Vec<SimSummaryResponse>,
}

#[derive(Clone, Debug, Default, PartialEq, Serialize)]
pub struct CachedAnalyticsSnapshot {
    pub analytics_summary: AnalyticsSummaryResponse,
    pub analytics_meta: AnalyticsMetaResponse,
}

#[derive(Clone, Debug, Default, PartialEq, Serialize)]
pub struct CachedSnapshot {
    pub summary: SummaryResponse,
    pub sims: Vec<SimSummaryResponse>,
}
