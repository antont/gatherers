use std::{collections::HashMap, sync::RwLock, time::Instant};

use crate::{
    protocol::{EventEnvelope, EventPayload, FoodDropPayload, FoodPickupPayload},
    summary::{
        AnalyticsSummaryResponse, CachedLiveSnapshot, LiveSummaryResponse, SimSummaryResponse,
    },
};

pub struct EventOutcome {
    pub sim_loose_food_count: usize,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub(crate) struct FoodPosition {
    pub x: f64,
    pub y: f64,
}

#[derive(Clone, Copy, Debug)]
struct FoodSlot {
    present: bool,
    x: f64,
    y: f64,
}

impl FoodSlot {
    fn new(x: f64, y: f64) -> Self {
        Self { present: true, x, y }
    }

}

fn parse_slot_index(food_id: &str) -> Option<usize> {
    food_id.parse::<usize>().ok()
}

#[derive(Clone, Debug, Default)]
struct SimState {
    foods: Vec<FoodSlot>,
    loose_food_count: usize,
    ant_count: usize,
    connected_at: Option<Instant>,
    total_events: usize,
    pickup_count: usize,
    drop_count: usize,
    turn_move_count: usize,
}

pub struct Store {
    cell_size: f64,
    shards: Vec<RwLock<HashMap<String, SimState>>>,
}

#[derive(Clone, Debug, Default)]
pub(crate) struct LiveSnapshotData {
    sims: Vec<SimSummaryResponse>,
    started_at: Option<Instant>,
    total_events: usize,
    loose_food_count: usize,
}

#[derive(Clone, Debug, Default)]
pub(crate) struct AnalyticsInputData {
    loose_food: Vec<FoodPosition>,
    cell_size: f64,
}

impl Default for Store {
    fn default() -> Self {
        Self::new(50.0)
    }
}

impl Store {
    const SHARD_COUNT: usize = 32;

    pub fn new(cell_size: f64) -> Self {
        Self {
            cell_size,
            shards: (0..Self::SHARD_COUNT)
                .map(|_| RwLock::new(HashMap::new()))
                .collect(),
        }
    }

    pub fn apply_event(&self, envelope: &EventEnvelope) -> Result<EventOutcome, String> {
        let sim_loose_food_count = match &envelope.payload {
            EventPayload::SimHello(payload) => {
                self.with_sim_state_mut(&envelope.sim_id, |state| {
                    state.record_event();
                    state.ant_count = payload.ant_count;
                    state.loose_food_count
                })
            }
            EventPayload::SimFoodSnapshot(payload) => {
                self.with_sim_state_mut(&envelope.sim_id, |state| {
                    state.record_event();
                    state.foods = payload
                        .foods
                        .iter()
                        .map(|food| FoodSlot::new(food.x as f64, food.y as f64))
                        .collect();
                    state.loose_food_count = state.foods.iter().filter(|s| s.present).count();
                    state.loose_food_count
                })
            }
            EventPayload::FoodPickup(payload) => self.record_pickup(&envelope.sim_id, payload),
            EventPayload::FoodDrop(payload) => self.record_drop(&envelope.sim_id, payload),
            EventPayload::AntTurnMove(_) => {
                self.with_sim_state_mut(&envelope.sim_id, |state| {
                    state.record_event();
                    state.turn_move_count += 1;
                    state.loose_food_count
                })
            }
            EventPayload::SimHeartbeat(_) | EventPayload::SimGoodbye(_) => {
                return Err(format!("unsupported event type: {}", envelope.event_type));
            }
        };
        Ok(EventOutcome {
            sim_loose_food_count,
        })
    }

    pub fn live_snapshot(&self) -> CachedLiveSnapshot {
        self.live_snapshot_data().into_cached_live_snapshot()
    }

    fn with_sim_state_mut<T>(&self, sim_id: &str, update: impl FnOnce(&mut SimState) -> T) -> T {
        let mut shard = self.shards[self.shard_index(sim_id)]
            .write()
            .expect("store shard lock poisoned");
        let state = shard.entry(sim_id.to_string()).or_default();
        update(state)
    }

    fn record_pickup(&self, sim_id: &str, payload: &FoodPickupPayload) -> usize {
        self.with_sim_state_mut(sim_id, |state| {
            state.record_event();
            state.pickup_count += 1;
            if let Some(idx) = parse_slot_index(&payload.food_id)
                && let Some(slot) = state.foods.get_mut(idx)
                && slot.present
            {
                slot.present = false;
                state.loose_food_count = state.loose_food_count.saturating_sub(1);
            }
            state.loose_food_count
        })
    }

    fn record_drop(&self, sim_id: &str, payload: &FoodDropPayload) -> usize {
        self.with_sim_state_mut(sim_id, |state| {
            state.record_event();
            state.drop_count += 1;
            if let Some(idx) = parse_slot_index(&payload.food_id)
                && let Some(slot) = state.foods.get_mut(idx)
            {
                if !slot.present {
                    state.loose_food_count += 1;
                }
                slot.present = true;
                slot.x = payload.x as f64;
                slot.y = payload.y as f64;
            }
            state.loose_food_count
        })
    }

    pub(crate) fn live_snapshot_data(&self) -> LiveSnapshotData {
        let mut sims = Vec::new();
        let mut started_at = None;
        let mut total_events = 0;
        let mut loose_food_count = 0;

        for shard in &self.shards {
            let shard = shard.read().expect("store shard lock poisoned");
            for (sim_id, sim) in shard.iter() {
                sims.push(SimSummaryResponse {
                    sim_id: sim_id.clone(),
                    ant_count: sim.ant_count,
                    pickup_count: sim.pickup_count,
                    drop_count: sim.drop_count,
                    turn_move_count: sim.turn_move_count,
                    loose_food_count: sim.loose_food_count,
                });
                total_events += sim.total_events;
                if let Some(connected_at) = sim.connected_at {
                    started_at = match started_at {
                        Some(existing) if existing <= connected_at => Some(existing),
                        _ => Some(connected_at),
                    };
                }
                loose_food_count += sim.loose_food_count;
            }
        }

        LiveSnapshotData {
            sims,
            started_at,
            total_events,
            loose_food_count,
        }
    }

    pub(crate) fn analytics_input_data(&self) -> AnalyticsInputData {
        let mut loose_food = Vec::new();

        for shard in &self.shards {
            let shard = shard.read().expect("store shard lock poisoned");
            for sim in shard.values() {
                for slot in &sim.foods {
                    if slot.present {
                        loose_food.push(FoodPosition { x: slot.x, y: slot.y });
                    }
                }
            }
        }

        AnalyticsInputData {
            loose_food,
            cell_size: self.cell_size,
        }
    }

    fn shard_index(&self, sim_id: &str) -> usize {
        shard_index(sim_id)
    }

    #[cfg(test)]
    pub(crate) fn hold_shard_for_test(&self, sim_id: &str) -> StoreShardWriteGuard<'_> {
        StoreShardWriteGuard {
            _guard: self.shards[self.shard_index(sim_id)]
                .write()
                .expect("store shard lock poisoned"),
        }
    }

    #[cfg(test)]
    pub(crate) fn try_hold_shard_for_test(&self, sim_id: &str) -> Option<StoreShardWriteGuard<'_>> {
        self.shards[self.shard_index(sim_id)]
            .try_write()
            .ok()
            .map(|guard| StoreShardWriteGuard { _guard: guard })
    }

    #[cfg(test)]
    pub(crate) fn shard_index_for_test(&self, sim_id: &str) -> usize {
        self.shard_index(sim_id)
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

impl LiveSnapshotData {
    fn summary(&self) -> LiveSummaryResponse {
        let (elapsed_seconds, events_per_second) = self.metrics();
        LiveSummaryResponse {
            connected_sim_count: self.sims.len(),
            loose_food_count: self.loose_food_count,
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

    pub(crate) fn into_cached_live_snapshot(self) -> CachedLiveSnapshot {
        CachedLiveSnapshot {
            live_summary: self.summary(),
            sims: self.sims,
        }
    }
}

impl AnalyticsInputData {
    pub(crate) fn summary(&self) -> AnalyticsSummaryResponse {
        if self.loose_food.is_empty() {
            return AnalyticsSummaryResponse::default();
        }

        AnalyticsSummaryResponse {
            occupied_cell_count: occupied_cell_count(&self.loose_food, self.cell_size),
            nearest_neighbor_mean_distance: mean_nearest_neighbor_distance(&self.loose_food),
        }
    }
}

fn shard_index(sim_id: &str) -> usize {
    sim_id.bytes().fold(0usize, |hash, byte| {
        hash.wrapping_mul(16777619).wrapping_add(byte as usize)
    }) % Store::SHARD_COUNT
}

#[cfg(test)]
pub(crate) struct StoreShardWriteGuard<'a> {
    _guard: std::sync::RwLockWriteGuard<'a, HashMap<String, SimState>>,
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

#[cfg(test)]
mod food_slot_tests {
    use super::*;
    use crate::protocol::*;

    fn make_envelope(sim_id: &str, payload: EventPayload) -> EventEnvelope {
        EventEnvelope {
            event_type: match &payload {
                EventPayload::SimHello(_) => "sim_hello",
                EventPayload::SimFoodSnapshot(_) => "sim_food_snapshot",
                EventPayload::FoodPickup(_) => "food_pickup",
                EventPayload::FoodDrop(_) => "food_drop",
                EventPayload::AntTurnMove(_) => "ant_turn_move",
                EventPayload::SimHeartbeat(_) => "sim_heartbeat",
                EventPayload::SimGoodbye(_) => "sim_goodbye",
            }
            .into(),
            sim_id: sim_id.into(),
            seq: 1,
            timestamp_ms: 1000,
            payload,
        }
    }

    fn snapshot_3_foods(sim_id: &str) -> EventEnvelope {
        make_envelope(
            sim_id,
            EventPayload::SimFoodSnapshot(FoodSnapshotPayload {
                foods: vec![
                    StartupFoodPayload { food_id: "0".into(), x: 10.0, y: 20.0 },
                    StartupFoodPayload { food_id: "1".into(), x: 30.0, y: 40.0 },
                    StartupFoodPayload { food_id: "2".into(), x: 50.0, y: 60.0 },
                ],
            }),
        )
    }

    #[test]
    fn snapshot_builds_stable_slot_array_with_fixed_count() {
        let store = Store::default();
        let outcome = store.apply_event(&snapshot_3_foods("sim-a")).unwrap();
        assert_eq!(outcome.sim_loose_food_count, 3);

        let analytics = store.analytics_input_data();
        assert_eq!(analytics.loose_food.len(), 3);
    }

    #[test]
    fn pickup_clears_slot_without_removing_it() {
        let store = Store::default();
        store.apply_event(&snapshot_3_foods("sim-a")).unwrap();

        let outcome = store
            .apply_event(&make_envelope(
                "sim-a",
                EventPayload::FoodPickup(FoodPickupPayload {
                    ant_id: None,
                    food_id: "1".into(),
                    x: None,
                    y: None,
                    direction_x: None,
                    direction_y: None,
                    frame: None,
                }),
            ))
            .unwrap();
        assert_eq!(outcome.sim_loose_food_count, 2);

        let analytics = store.analytics_input_data();
        assert_eq!(analytics.loose_food.len(), 2, "analytics should see 2 present foods");
    }

    #[test]
    fn duplicate_food_drop_updates_existing_slot_not_count() {
        let store = Store::default();
        store.apply_event(&snapshot_3_foods("sim-a")).unwrap();

        store
            .apply_event(&make_envelope(
                "sim-a",
                EventPayload::FoodPickup(FoodPickupPayload {
                    ant_id: None,
                    food_id: "0".into(),
                    x: None,
                    y: None,
                    direction_x: None,
                    direction_y: None,
                    frame: None,
                }),
            ))
            .unwrap();

        let outcome = store
            .apply_event(&make_envelope(
                "sim-a",
                EventPayload::FoodDrop(FoodDropPayload {
                    ant_id: None,
                    food_id: "0".into(),
                    x: 99.0,
                    y: 88.0,
                    direction_x: None,
                    direction_y: None,
                    frame: None,
                }),
            ))
            .unwrap();
        assert_eq!(outcome.sim_loose_food_count, 3, "drop of picked-up slot restores count");

        let again = store
            .apply_event(&make_envelope(
                "sim-a",
                EventPayload::FoodDrop(FoodDropPayload {
                    ant_id: None,
                    food_id: "0".into(),
                    x: 77.0,
                    y: 66.0,
                    direction_x: None,
                    direction_y: None,
                    frame: None,
                }),
            ))
            .unwrap();
        assert_eq!(
            again.sim_loose_food_count, 3,
            "duplicate drop of already-present slot must not inflate count"
        );
    }

    #[test]
    fn out_of_range_slot_id_does_not_change_count() {
        let store = Store::default();
        store.apply_event(&snapshot_3_foods("sim-a")).unwrap();

        let outcome = store
            .apply_event(&make_envelope(
                "sim-a",
                EventPayload::FoodDrop(FoodDropPayload {
                    ant_id: None,
                    food_id: "999".into(),
                    x: 1.0,
                    y: 2.0,
                    direction_x: None,
                    direction_y: None,
                    frame: None,
                }),
            ))
            .unwrap();
        assert_eq!(
            outcome.sim_loose_food_count, 3,
            "out-of-range drop must not inflate count"
        );
    }

    #[test]
    fn malformed_slot_id_does_not_change_count() {
        let store = Store::default();
        store.apply_event(&snapshot_3_foods("sim-a")).unwrap();

        let outcome = store
            .apply_event(&make_envelope(
                "sim-a",
                EventPayload::FoodPickup(FoodPickupPayload {
                    ant_id: None,
                    food_id: "not-a-number".into(),
                    x: None,
                    y: None,
                    direction_x: None,
                    direction_y: None,
                    frame: None,
                }),
            ))
            .unwrap();
        assert_eq!(
            outcome.sim_loose_food_count, 3,
            "malformed food_id must not change count"
        );
    }

    #[test]
    fn analytics_scan_returns_only_present_food_positions() {
        let store = Store::default();
        store.apply_event(&snapshot_3_foods("sim-a")).unwrap();

        store
            .apply_event(&make_envelope(
                "sim-a",
                EventPayload::FoodPickup(FoodPickupPayload {
                    ant_id: None,
                    food_id: "1".into(),
                    x: None,
                    y: None,
                    direction_x: None,
                    direction_y: None,
                    frame: None,
                }),
            ))
            .unwrap();

        let analytics = store.analytics_input_data();
        assert_eq!(analytics.loose_food.len(), 2);

        let positions: Vec<(f64, f64)> = analytics
            .loose_food
            .iter()
            .map(|p| (p.x, p.y))
            .collect();
        assert!(positions.contains(&(10.0, 20.0)), "slot 0 should be present");
        assert!(
            !positions.contains(&(30.0, 40.0)),
            "slot 1 was picked up, should not appear"
        );
        assert!(positions.contains(&(50.0, 60.0)), "slot 2 should be present");
    }

    #[test]
    fn food_drop_updates_slot_position() {
        let store = Store::default();
        store.apply_event(&snapshot_3_foods("sim-a")).unwrap();

        store
            .apply_event(&make_envelope(
                "sim-a",
                EventPayload::FoodPickup(FoodPickupPayload {
                    ant_id: None,
                    food_id: "0".into(),
                    x: None,
                    y: None,
                    direction_x: None,
                    direction_y: None,
                    frame: None,
                }),
            ))
            .unwrap();

        store
            .apply_event(&make_envelope(
                "sim-a",
                EventPayload::FoodDrop(FoodDropPayload {
                    ant_id: None,
                    food_id: "0".into(),
                    x: 99.0,
                    y: 88.0,
                    direction_x: None,
                    direction_y: None,
                    frame: None,
                }),
            ))
            .unwrap();

        let analytics = store.analytics_input_data();
        let positions: Vec<(f64, f64)> = analytics
            .loose_food
            .iter()
            .map(|p| (p.x, p.y))
            .collect();
        assert!(
            positions.contains(&(99.0, 88.0)),
            "slot 0 should be at new drop position"
        );
    }
}
