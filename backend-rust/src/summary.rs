use serde::Serialize;

#[derive(Clone, Debug, Default, PartialEq, Serialize)]
pub struct SummaryResponse {
    pub connected_sim_count: usize,
    pub loose_food_count: usize,
    pub occupied_cell_count: usize,
    pub nearest_neighbor_mean_distance: f64,
    pub elapsed_seconds: f64,
    pub events_per_second: f64,
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
pub struct CachedSnapshot {
    pub summary: SummaryResponse,
    pub sims: Vec<SimSummaryResponse>,
}
