use std::{
    collections::HashMap,
    sync::{
        Arc, OnceLock, RwLock,
        atomic::{AtomicU64, AtomicUsize, Ordering},
    },
    time::Instant,
};

use crate::{
    protocol::{EventEnvelope, EventPayload},
    summary::{AnalyticsSummaryResponse, SimSummaryResponse},
};

// Packed food slot encoding: upper 32 bits = x.to_bits(), lower 32 bits = y.to_bits().
// ABSENT uses a quiet NaN with payload 1 in both halves.  Normal simulation
// coordinates are always finite so this pattern never occurs naturally.
const ABSENT_HALF: u32 = 0x7FC0_0001;
const ABSENT_BITS: u64 = ((ABSENT_HALF as u64) << 32) | (ABSENT_HALF as u64);

fn pack_present(x: f32, y: f32) -> u64 {
    ((x.to_bits() as u64) << 32) | (y.to_bits() as u64)
}

/// One food item's state as a single 64-bit atomic.
/// A single `store` / `load` is the only synchronization needed.
struct AtomicFoodSlot {
    bits: AtomicU64,
}

impl AtomicFoodSlot {
    fn new_present(x: f32, y: f32) -> Self {
        Self { bits: AtomicU64::new(pack_present(x, y)) }
    }

    /// Mark this slot as absent (food picked up).
    fn store_absent(&self) {
        self.bits.store(ABSENT_BITS, Ordering::Relaxed);
    }

    /// Mark this slot as present at `(x, y)` (food dropped).
    fn store_present(&self, x: f32, y: f32) {
        self.bits.store(pack_present(x, y), Ordering::Relaxed);
    }

    /// `Some((x, y))` if present, `None` if absent.
    fn load(&self) -> Option<(f32, f32)> {
        let word = self.bits.load(Ordering::Relaxed);
        if word == ABSENT_BITS {
            return None;
        }
        let x_bits = (word >> 32) as u32;
        let y_bits = word as u32;
        Some((f32::from_bits(x_bits), f32::from_bits(y_bits)))
    }

    fn is_present(&self) -> bool {
        self.bits.load(Ordering::Relaxed) != ABSENT_BITS
    }
}

/// Outcome returned to the caller so it can update global live counters.
pub struct EventOutcome {
    pub loose_food_delta: isize,
    pub ant_count: usize,
    pub pickup_count: usize,
    pub drop_count: usize,
    pub turn_move_count: usize,
    pub sim_loose_food_count: usize,
}

/// Per-sim state owned exclusively by one WebSocket task.
/// All fields are atomics so analytics workers can read them without locks.
pub struct SimHandle {
    pub sim_id: String,
    pub connected_at: OnceLock<Instant>,
    /// Fixed-size food slot array, installed exactly once by the first
    /// `sim_food_snapshot` event.  `food_id` is a direct slot index.
    foods: OnceLock<Box<[AtomicFoodSlot]>>,
    pub ant_count: AtomicUsize,
    pub total_events: AtomicUsize,
    pub pickup_count: AtomicUsize,
    pub drop_count: AtomicUsize,
    pub turn_move_count: AtomicUsize,
    pub loose_food_count: AtomicUsize,
}

impl SimHandle {
    pub fn new(sim_id: String) -> Self {
        Self {
            sim_id,
            connected_at: OnceLock::new(),
            foods: OnceLock::new(),
            ant_count: AtomicUsize::new(0),
            total_events: AtomicUsize::new(0),
            pickup_count: AtomicUsize::new(0),
            drop_count: AtomicUsize::new(0),
            turn_move_count: AtomicUsize::new(0),
            loose_food_count: AtomicUsize::new(0),
        }
    }

    fn record_event(&self) {
        self.connected_at.get_or_init(Instant::now);
        self.total_events.fetch_add(1, Ordering::Relaxed);
    }

    fn apply_food_snapshot(
        &self,
        payload: &crate::protocol::FoodSnapshotPayload,
    ) -> Result<isize, String> {
        let count = payload.foods.len();
        let mut positions = vec![None; count];
        for food in &payload.foods {
            if food.food_id >= count {
                return Err(format!(
                    "sim_food_snapshot slot id {} out of range for {} foods",
                    food.food_id, count
                ));
            }
            if positions[food.food_id].is_some() {
                return Err(format!(
                    "sim_food_snapshot duplicated slot id {}",
                    food.food_id
                ));
            }
            positions[food.food_id] = Some((food.x, food.y));
        }
        if positions.iter().any(Option::is_none) {
            return Err("sim_food_snapshot must provide a dense slot set".into());
        }

        self.record_event();

        if let Some(existing) = self.foods.get() {
            if existing.len() != count {
                return Err(format!(
                    "sim_food_snapshot shape mismatch: expected {} foods, got {}",
                    existing.len(), count
                ));
            }
            for (slot, position) in existing.iter().zip(positions.into_iter()) {
                let (x, y) = position.expect("validated dense slot set");
                slot.store_present(x, y);
            }
        } else {
            let slots: Box<[AtomicFoodSlot]> = positions
                .into_iter()
                .map(|position| {
                    let (x, y) = position.expect("validated dense slot set");
                    AtomicFoodSlot::new_present(x, y)
                })
                .collect::<Vec<_>>()
                .into_boxed_slice();
            let _ = self.foods.set(slots);
        }

        let prev = self.loose_food_count.swap(count, Ordering::Relaxed);
        Ok(count as isize - prev as isize)
    }

    /// Apply one event.  Returns the signed change to the loose-food count
    /// so the caller can update the global aggregate.
    ///
    /// Single-writer contract: only one task ever writes to a given
    /// `SimHandle`, so the load-check-store sequences on food slots are
    /// not racy with respect to other writers.
    pub fn apply_event(&self, envelope: &EventEnvelope) -> Result<EventOutcome, String> {
        let loose_food_delta = match &envelope.payload {
            EventPayload::SimHello(payload) => {
                self.record_event();
                self.ant_count.store(payload.ant_count, Ordering::Relaxed);
                0
            }
            EventPayload::SimFoodSnapshot(payload) => {
                self.apply_food_snapshot(payload)?
            }
            EventPayload::FoodPickup(payload) => {
                self.record_event();
                self.pickup_count.fetch_add(1, Ordering::Relaxed);
                self.foods
                    .get()
                    .and_then(|foods| foods.get(payload.food_id))
                    .map(|slot| {
                        if slot.is_present() {
                            slot.store_absent();
                            self.loose_food_count.fetch_sub(1, Ordering::Relaxed);
                            -1isize
                        } else {
                            0
                        }
                    })
                    .unwrap_or(0)
            }
            EventPayload::FoodDrop(payload) => {
                self.record_event();
                self.drop_count.fetch_add(1, Ordering::Relaxed);
                self.foods
                    .get()
                    .and_then(|foods| foods.get(payload.food_id))
                    .map(|slot| {
                        let was_absent = !slot.is_present();
                        slot.store_present(payload.x, payload.y);
                        if was_absent {
                            self.loose_food_count.fetch_add(1, Ordering::Relaxed);
                            1isize
                        } else {
                            0
                        }
                    })
                    .unwrap_or(0)
            }
            EventPayload::AntTurnMove(_) => {
                self.record_event();
                self.turn_move_count.fetch_add(1, Ordering::Relaxed);
                0
            }
            EventPayload::SimHeartbeat(_) => {
                self.record_event();
                0
            }
            EventPayload::SimGoodbye(_) => {
                return Err(format!("unsupported event type: {}", envelope.event_type));
            }
        };
        Ok(EventOutcome {
            loose_food_delta,
            ant_count: self.ant_count.load(Ordering::Relaxed),
            pickup_count: self.pickup_count.load(Ordering::Relaxed),
            drop_count: self.drop_count.load(Ordering::Relaxed),
            turn_move_count: self.turn_move_count.load(Ordering::Relaxed),
            sim_loose_food_count: self.loose_food_count.load(Ordering::Relaxed),
        })
    }

    pub fn sim_summary(&self) -> SimSummaryResponse {
        SimSummaryResponse {
            sim_id: self.sim_id.clone(),
            ant_count: self.ant_count.load(Ordering::Relaxed),
            pickup_count: self.pickup_count.load(Ordering::Relaxed),
            drop_count: self.drop_count.load(Ordering::Relaxed),
            turn_move_count: self.turn_move_count.load(Ordering::Relaxed),
            loose_food_count: self.loose_food_count.load(Ordering::Relaxed),
        }
    }
}

/// Global sim registry.  The lock is used only for connect (first event from
/// a sim) and for readers that need to clone all handles.  Steady-state ingest
/// bypasses the registry entirely via a cached `Arc<SimHandle>`.
pub struct Registry {
    sims: RwLock<HashMap<String, Arc<SimHandle>>>,
    pub cell_size: f64,
    #[cfg(test)]
    all_handles_calls: AtomicUsize,
}

impl Default for Registry {
    fn default() -> Self {
        Self::new(50.0)
    }
}

impl Registry {
    pub fn new(cell_size: f64) -> Self {
        Self {
            sims: RwLock::new(HashMap::new()),
            cell_size,
            #[cfg(test)]
            all_handles_calls: AtomicUsize::new(0),
        }
    }

    /// Return existing handle or create a new one.  Read-first to avoid write
    /// lock contention on the hot path after initial registration.
    pub fn get_or_create(&self, sim_id: &str) -> Arc<SimHandle> {
        {
            let sims = self.sims.read().expect("registry read lock poisoned");
            if let Some(handle) = sims.get(sim_id) {
                return handle.clone();
            }
        }
        let mut sims = self.sims.write().expect("registry write lock poisoned");
        sims.entry(sim_id.to_string())
            .or_insert_with(|| Arc::new(SimHandle::new(sim_id.to_string())))
            .clone()
    }

    /// Clone all current sim handles.  Brief read lock only.
    pub fn all_handles(&self) -> Vec<Arc<SimHandle>> {
        #[cfg(test)]
        self.all_handles_calls.fetch_add(1, Ordering::Relaxed);
        self.sims
            .read()
            .expect("registry read lock poisoned")
            .values()
            .cloned()
            .collect()
    }

    pub fn connected_sim_count(&self) -> usize {
        self.sims.read().expect("registry read lock poisoned").len()
    }

    pub fn remove_if_same_handle(&self, sim_id: &str, handle: &Arc<SimHandle>) -> bool {
        let mut sims = self.sims.write().expect("registry write lock poisoned");
        match sims.get(sim_id) {
            Some(existing) if Arc::ptr_eq(existing, handle) => {
                sims.remove(sim_id);
                true
            }
            _ => false,
        }
    }

    #[cfg(test)]
    pub fn all_handles_calls_for_test(&self) -> usize {
        self.all_handles_calls.load(Ordering::Relaxed)
    }

    /// Gather loose food positions for analytics computation.
    /// Reads atomic food slots directly -- no locks held during the scan.
    pub(crate) fn analytics_input_data(&self) -> AnalyticsInputData {
        let handles = self.all_handles();
        let mut loose_food = Vec::new();
        for handle in &handles {
            if let Some(foods) = handle.foods.get() {
                for slot in foods.iter() {
                    if let Some((x, y)) = slot.load() {
                        loose_food.push(FoodPosition { x: x as f64, y: y as f64 });
                    }
                }
            }
        }
        AnalyticsInputData {
            loose_food,
            cell_size: self.cell_size,
        }
    }
}

#[derive(Clone, Debug, Default, PartialEq)]
pub(crate) struct FoodPosition {
    pub x: f64,
    pub y: f64,
}

#[derive(Clone, Debug, Default)]
pub(crate) struct AnalyticsInputData {
    pub loose_food: Vec<FoodPosition>,
    pub cell_size: f64,
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
                    Some(
                        ((position.x - other.x).powi(2) + (position.y - other.y).powi(2)).sqrt(),
                    )
                })
                .fold(f64::MAX, f64::min)
        })
        .sum::<f64>();

    total / positions.len() as f64
}

#[cfg(test)]
mod atomic_slot_tests {
    use super::*;
    use crate::protocol::*;

    // ---- AtomicFoodSlot unit tests ----

    #[test]
    fn new_present_slot_loads_back_same_coords() {
        let slot = AtomicFoodSlot::new_present(1.5, 2.5);
        assert_eq!(slot.load(), Some((1.5, 2.5)));
        assert!(slot.is_present());
    }

    #[test]
    fn store_absent_makes_load_return_none() {
        let slot = AtomicFoodSlot::new_present(10.0, 20.0);
        slot.store_absent();
        assert_eq!(slot.load(), None);
        assert!(!slot.is_present());
    }

    #[test]
    fn store_present_after_absent_restores_slot() {
        let slot = AtomicFoodSlot::new_present(10.0, 20.0);
        slot.store_absent();
        slot.store_present(99.0, 88.0);
        assert_eq!(slot.load(), Some((99.0, 88.0)));
        assert!(slot.is_present());
    }

    #[test]
    fn zero_coordinates_are_not_confused_with_absent() {
        let slot = AtomicFoodSlot::new_present(0.0, 0.0);
        assert_eq!(slot.load(), Some((0.0, 0.0)));
    }

    #[test]
    fn negative_coordinates_round_trip() {
        let slot = AtomicFoodSlot::new_present(-128.5, -256.75);
        assert_eq!(slot.load(), Some((-128.5, -256.75)));
    }

    // ---- SimHandle event tests ----

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
                    StartupFoodPayload { food_id: 0, x: 10.0, y: 20.0 },
                    StartupFoodPayload { food_id: 1, x: 30.0, y: 40.0 },
                    StartupFoodPayload { food_id: 2, x: 50.0, y: 60.0 },
                ],
            }),
        )
    }

    fn snapshot_with_foods(sim_id: &str, foods: Vec<StartupFoodPayload>) -> EventEnvelope {
        make_envelope(
            sim_id,
            EventPayload::SimFoodSnapshot(FoodSnapshotPayload { foods }),
        )
    }

    #[test]
    fn snapshot_installs_slot_array_with_all_slots_present() {
        let handle = SimHandle::new("sim-a".into());
        let outcome = handle.apply_event(&snapshot_3_foods("sim-a")).unwrap();
        assert_eq!(outcome.loose_food_delta, 3);
        assert_eq!(handle.loose_food_count.load(Ordering::Relaxed), 3);
    }

    #[test]
    fn repeated_same_shape_snapshot_refreshes_slot_positions() {
        let handle = SimHandle::new("sim-a".into());
        handle.apply_event(&snapshot_3_foods("sim-a")).unwrap();

        let outcome = handle
            .apply_event(&snapshot_with_foods(
                "sim-a",
                vec![
                    StartupFoodPayload { food_id: 0, x: 101.0, y: 201.0 },
                    StartupFoodPayload { food_id: 1, x: 301.0, y: 401.0 },
                    StartupFoodPayload { food_id: 2, x: 501.0, y: 601.0 },
                ],
            ))
            .unwrap();

        assert_eq!(outcome.loose_food_delta, 0);
        assert_eq!(handle.loose_food_count.load(Ordering::Relaxed), 3);
        let foods = handle.foods.get().expect("slot array should stay installed");
        assert_eq!(foods[0].load(), Some((101.0, 201.0)));
        assert_eq!(foods[1].load(), Some((301.0, 401.0)));
        assert_eq!(foods[2].load(), Some((501.0, 601.0)));
    }

    #[test]
    fn repeated_mismatched_shape_snapshot_is_rejected() {
        let handle = SimHandle::new("sim-a".into());
        handle.apply_event(&snapshot_3_foods("sim-a")).unwrap();

        let error = match handle.apply_event(&snapshot_with_foods(
            "sim-a",
            vec![
                StartupFoodPayload { food_id: 0, x: 101.0, y: 201.0 },
                StartupFoodPayload { food_id: 1, x: 301.0, y: 401.0 },
            ],
        )) {
            Ok(_) => panic!("mismatched snapshot shape should be rejected"),
            Err(error) => error,
        };

        assert!(
            error.contains("sim_food_snapshot"),
            "expected snapshot shape error, got {error}"
        );
        assert_eq!(handle.loose_food_count.load(Ordering::Relaxed), 3);
        let foods = handle.foods.get().expect("original slot array should remain installed");
        assert_eq!(foods[0].load(), Some((10.0, 20.0)));
        assert_eq!(foods[1].load(), Some((30.0, 40.0)));
        assert_eq!(foods[2].load(), Some((50.0, 60.0)));
    }

    #[test]
    fn pickup_clears_slot_and_decrements_count() {
        let handle = SimHandle::new("sim-a".into());
        handle.apply_event(&snapshot_3_foods("sim-a")).unwrap();

        let outcome = handle
            .apply_event(&make_envelope(
                "sim-a",
                EventPayload::FoodPickup(FoodPickupPayload {
                    ant_id: None,
                    food_id: 1,
                    x: None,
                    y: None,
                    direction_x: None,
                    direction_y: None,
                    frame: None,
                }),
            ))
            .unwrap();

        assert_eq!(outcome.loose_food_delta, -1);
        assert_eq!(handle.loose_food_count.load(Ordering::Relaxed), 2);
        assert!(!handle.foods.get().unwrap()[1].is_present());
    }

    #[test]
    fn drop_restores_slot_and_increments_count() {
        let handle = SimHandle::new("sim-a".into());
        handle.apply_event(&snapshot_3_foods("sim-a")).unwrap();

        // pick up first
        handle
            .apply_event(&make_envelope(
                "sim-a",
                EventPayload::FoodPickup(FoodPickupPayload {
                    ant_id: None,
                    food_id: 0,
                    x: None,
                    y: None,
                    direction_x: None,
                    direction_y: None,
                    frame: None,
                }),
            ))
            .unwrap();

        let outcome = handle
            .apply_event(&make_envelope(
                "sim-a",
                EventPayload::FoodDrop(FoodDropPayload {
                    ant_id: None,
                    food_id: 0,
                    x: 99.0,
                    y: 88.0,
                    direction_x: None,
                    direction_y: None,
                    frame: None,
                }),
            ))
            .unwrap();

        assert_eq!(outcome.loose_food_delta, 1);
        assert_eq!(handle.loose_food_count.load(Ordering::Relaxed), 3);
        assert_eq!(
            handle.foods.get().unwrap()[0].load(),
            Some((99.0, 88.0)),
            "drop position should be stored in slot"
        );
    }

    #[test]
    fn duplicate_drop_does_not_inflate_count() {
        let handle = SimHandle::new("sim-a".into());
        handle.apply_event(&snapshot_3_foods("sim-a")).unwrap();

        handle
            .apply_event(&make_envelope(
                "sim-a",
                EventPayload::FoodPickup(FoodPickupPayload {
                    ant_id: None,
                    food_id: 0,
                    x: None,
                    y: None,
                    direction_x: None,
                    direction_y: None,
                    frame: None,
                }),
            ))
            .unwrap();
        handle
            .apply_event(&make_envelope(
                "sim-a",
                EventPayload::FoodDrop(FoodDropPayload {
                    ant_id: None,
                    food_id: 0,
                    x: 99.0,
                    y: 88.0,
                    direction_x: None,
                    direction_y: None,
                    frame: None,
                }),
            ))
            .unwrap();
        let again = handle
            .apply_event(&make_envelope(
                "sim-a",
                EventPayload::FoodDrop(FoodDropPayload {
                    ant_id: None,
                    food_id: 0,
                    x: 77.0,
                    y: 66.0,
                    direction_x: None,
                    direction_y: None,
                    frame: None,
                }),
            ))
            .unwrap();

        assert_eq!(again.loose_food_delta, 0, "duplicate drop must not inflate count");
        assert_eq!(handle.loose_food_count.load(Ordering::Relaxed), 3);
    }

    #[test]
    fn out_of_range_pickup_does_not_change_count() {
        let handle = SimHandle::new("sim-a".into());
        handle.apply_event(&snapshot_3_foods("sim-a")).unwrap();

        let outcome = handle
            .apply_event(&make_envelope(
                "sim-a",
                EventPayload::FoodPickup(FoodPickupPayload {
                    ant_id: None,
                    food_id: usize::MAX,
                    x: None,
                    y: None,
                    direction_x: None,
                    direction_y: None,
                    frame: None,
                }),
            ))
            .unwrap();

        assert_eq!(outcome.loose_food_delta, 0);
        assert_eq!(handle.loose_food_count.load(Ordering::Relaxed), 3);
    }

    #[test]
    fn out_of_range_drop_does_not_change_count() {
        let handle = SimHandle::new("sim-a".into());
        handle.apply_event(&snapshot_3_foods("sim-a")).unwrap();

        let outcome = handle
            .apply_event(&make_envelope(
                "sim-a",
                EventPayload::FoodDrop(FoodDropPayload {
                    ant_id: None,
                    food_id: 999,
                    x: 1.0,
                    y: 2.0,
                    direction_x: None,
                    direction_y: None,
                    frame: None,
                }),
            ))
            .unwrap();

        assert_eq!(outcome.loose_food_delta, 0);
        assert_eq!(handle.loose_food_count.load(Ordering::Relaxed), 3);
    }

    #[test]
    fn analytics_scan_returns_only_present_food_positions() {
        let reg = Registry::default();
        let handle = reg.get_or_create("sim-a");
        handle.apply_event(&snapshot_3_foods("sim-a")).unwrap();
        handle
            .apply_event(&make_envelope(
                "sim-a",
                EventPayload::FoodPickup(FoodPickupPayload {
                    ant_id: None,
                    food_id: 1,
                    x: None,
                    y: None,
                    direction_x: None,
                    direction_y: None,
                    frame: None,
                }),
            ))
            .unwrap();

        let analytics = reg.analytics_input_data();
        assert_eq!(analytics.loose_food.len(), 2);
        let positions: Vec<(f64, f64)> =
            analytics.loose_food.iter().map(|p| (p.x, p.y)).collect();
        assert!(positions.contains(&(10.0, 20.0)), "slot 0 should be present");
        assert!(!positions.contains(&(30.0, 40.0)), "slot 1 was picked up");
        assert!(positions.contains(&(50.0, 60.0)), "slot 2 should be present");
    }

    #[test]
    fn food_drop_updates_slot_position_visible_in_analytics() {
        let reg = Registry::default();
        let handle = reg.get_or_create("sim-a");
        handle.apply_event(&snapshot_3_foods("sim-a")).unwrap();
        handle
            .apply_event(&make_envelope(
                "sim-a",
                EventPayload::FoodPickup(FoodPickupPayload {
                    ant_id: None,
                    food_id: 0,
                    x: None,
                    y: None,
                    direction_x: None,
                    direction_y: None,
                    frame: None,
                }),
            ))
            .unwrap();
        handle
            .apply_event(&make_envelope(
                "sim-a",
                EventPayload::FoodDrop(FoodDropPayload {
                    ant_id: None,
                    food_id: 0,
                    x: 99.0,
                    y: 88.0,
                    direction_x: None,
                    direction_y: None,
                    frame: None,
                }),
            ))
            .unwrap();

        let analytics = reg.analytics_input_data();
        let positions: Vec<(f64, f64)> =
            analytics.loose_food.iter().map(|p| (p.x, p.y)).collect();
        assert!(positions.contains(&(99.0, 88.0)), "slot 0 should be at new drop position");
    }
}
