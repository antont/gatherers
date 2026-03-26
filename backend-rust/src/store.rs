use std::{collections::HashMap, time::Instant};

use crate::{
    protocol::{EventEnvelope, EventPayload, FoodDropPayload, FoodPickupPayload},
    summary::{CachedSnapshot, SimSummaryResponse, SummaryResponse},
};

#[derive(Clone, Copy, Debug, PartialEq)]
struct FoodPosition {
    x: f64,
    y: f64,
}

#[derive(Clone, Debug, Default)]
struct SimState {
    loose_food: HashMap<String, FoodPosition>,
    ant_count: usize,
    connected_at: Option<Instant>,
    total_events: usize,
    pickup_count: usize,
    drop_count: usize,
    turn_move_count: usize,
}

#[derive(Clone, Debug)]
pub struct Store {
    cell_size: f64,
    sims: HashMap<String, SimState>,
}

#[derive(Clone, Debug, Default)]
pub(crate) struct DashboardSnapshotData {
    sims: Vec<SimSummaryResponse>,
    loose_food: Vec<FoodPosition>,
    started_at: Option<Instant>,
    total_events: usize,
    cell_size: f64,
}

impl Default for Store {
    fn default() -> Self {
        Self::new(50.0)
    }
}

impl Store {
    pub fn new(cell_size: f64) -> Self {
        Self {
            cell_size,
            sims: HashMap::new(),
        }
    }

    pub fn apply_event(&mut self, envelope: &EventEnvelope) -> Result<(), String> {
        match &envelope.payload {
            EventPayload::SimHello(payload) => {
                let state = self.ensure_sim(&envelope.sim_id);
                state.record_event();
                state.ant_count = payload.ant_count;
            }
            EventPayload::SimFoodSnapshot(payload) => {
                let state = self.ensure_sim(&envelope.sim_id);
                state.record_event();
                state.loose_food = payload
                    .foods
                    .iter()
                    .map(|food| {
                        (
                            food.food_id.clone(),
                            FoodPosition {
                                x: food.x as f64,
                                y: food.y as f64,
                            },
                        )
                    })
                    .collect();
            }
            EventPayload::FoodPickup(payload) => self.record_pickup(&envelope.sim_id, payload),
            EventPayload::FoodDrop(payload) => self.record_drop(&envelope.sim_id, payload),
            EventPayload::AntTurnMove(_) => {
                let state = self.ensure_sim(&envelope.sim_id);
                state.record_event();
                state.turn_move_count += 1;
            }
            EventPayload::SimHeartbeat(_) | EventPayload::SimGoodbye(_) => {
                return Err(format!("unsupported event type: {}", envelope.event_type));
            }
        }
        Ok(())
    }

    pub fn snapshot(&self) -> CachedSnapshot {
        self.snapshot_data().into_cached_snapshot()
    }

    fn ensure_sim(&mut self, sim_id: &str) -> &mut SimState {
        self.sims.entry(sim_id.to_string()).or_default()
    }

    fn record_pickup(&mut self, sim_id: &str, payload: &FoodPickupPayload) {
        let state = self.ensure_sim(sim_id);
        state.record_event();
        state.loose_food.remove(&payload.food_id);
        state.pickup_count += 1;
    }

    fn record_drop(&mut self, sim_id: &str, payload: &FoodDropPayload) {
        let state = self.ensure_sim(sim_id);
        state.record_event();
        state.loose_food.insert(
            payload.food_id.clone(),
            FoodPosition {
                x: payload.x as f64,
                y: payload.y as f64,
            },
        );
        state.drop_count += 1;
    }

    pub(crate) fn snapshot_data(&self) -> DashboardSnapshotData {
        let mut sims = Vec::with_capacity(self.sims.len());
        let mut loose_food = Vec::new();
        let mut started_at = None;
        let mut total_events = 0;

        for (sim_id, sim) in &self.sims {
            sims.push(SimSummaryResponse {
                sim_id: sim_id.clone(),
                ant_count: sim.ant_count,
                pickup_count: sim.pickup_count,
                drop_count: sim.drop_count,
                turn_move_count: sim.turn_move_count,
                loose_food_count: sim.loose_food.len(),
            });
            total_events += sim.total_events;
            if let Some(connected_at) = sim.connected_at {
                started_at = match started_at {
                    Some(existing) if existing <= connected_at => Some(existing),
                    _ => Some(connected_at),
                };
            }
            loose_food.extend(sim.loose_food.values().copied());
        }

        DashboardSnapshotData {
            sims,
            loose_food,
            started_at,
            total_events,
            cell_size: self.cell_size,
        }
    }
}

impl SimState {
    fn record_event(&mut self) {
        if self.connected_at.is_none() {
            self.connected_at = Some(Instant::now());
        }
        self.total_events += 1;
    }
}

impl DashboardSnapshotData {
    fn summary(&self) -> SummaryResponse {
        let (elapsed_seconds, events_per_second) = self.metrics();
        if self.loose_food.is_empty() {
            return SummaryResponse {
                connected_sim_count: self.sims.len(),
                elapsed_seconds,
                events_per_second,
                ..SummaryResponse::default()
            };
        }

        SummaryResponse {
            connected_sim_count: self.sims.len(),
            loose_food_count: self.loose_food.len(),
            occupied_cell_count: occupied_cell_count(&self.loose_food, self.cell_size),
            nearest_neighbor_mean_distance: mean_nearest_neighbor_distance(&self.loose_food),
            elapsed_seconds,
            events_per_second,
        }
    }

    fn metrics(&self) -> (f64, f64) {
        let Some(started_at) = self.started_at else {
            return (0.0, 0.0);
        };
        let elapsed_seconds = started_at.elapsed().as_secs_f64();
        if elapsed_seconds <= 0.0 {
            return (0.0, 0.0);
        }
        (elapsed_seconds, self.total_events as f64 / elapsed_seconds)
    }

    pub(crate) fn into_cached_snapshot(self) -> CachedSnapshot {
        CachedSnapshot {
            summary: self.summary(),
            sims: self.sims,
        }
    }
}

fn occupied_cell_count(positions: &[FoodPosition], cell_size: f64) -> usize {
    let effective_cell_size = if cell_size > 0.0 { cell_size } else { 1.0 };
    let mut cells: HashMap<(i64, i64), ()> = HashMap::new();
    for position in positions {
        let cell = (
            (position.x / effective_cell_size).floor() as i64,
            (position.y / effective_cell_size).floor() as i64,
        );
        cells.insert(cell, ());
    }
    cells.len()
}

fn mean_nearest_neighbor_distance(positions: &[FoodPosition]) -> f64 {
    if positions.len() < 2 {
        return 0.0;
    }

    let total = positions
        .iter()
        .enumerate()
        .map(|(index, position)| {
            positions
                .iter()
                .enumerate()
                .filter_map(|(other_index, other)| {
                    if index == other_index {
                        return None;
                    }
                    Some(((position.x - other.x).powi(2) + (position.y - other.y).powi(2)).sqrt())
                })
                .fold(f64::MAX, f64::min)
        })
        .sum::<f64>();

    total / positions.len() as f64
}
