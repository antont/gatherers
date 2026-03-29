use std::{collections::HashMap, sync::{
    Arc, Once, RwLock,
    atomic::{AtomicBool, Ordering},
}};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use axum::{
    Json, Router,
    extract::{
        Query, State,
        ws::{Message, WebSocket, WebSocketUpgrade},
    },
    response::{Html, IntoResponse},
    routing::get,
};
use futures_util::StreamExt;
use serde::Deserialize;
use tokio::net::TcpListener;
use tokio::sync::{Mutex, Notify, broadcast};

use crate::{
    dashboard::render_dashboard,
    protocol::{EventEnvelope, EventPayload},
    store::{Registry, SimHandle},
    summary::{
        AnalyticsMetaResponse, CachedAnalyticsSnapshot, CachedLiveSnapshot, CachedSnapshot,
        BreakpointTotalsResponse, SimSummaryResponse, SummaryResponse,
    },
};

#[derive(Clone)]
pub struct AppState {
    inner: Arc<AppStateInner>,
}

struct AppStateInner {
    registry: Registry,
    live_snapshot: RwLock<LiveCacheState>,
    analytics_snapshot: RwLock<CachedAnalyticsSnapshot>,
    refresh: Mutex<RefreshState>,
    analytics_dirty: AtomicBool,
    last_snapshot_copy_micros: std::sync::atomic::AtomicU64,
    last_snapshot_compute_micros: std::sync::atomic::AtomicU64,
    refresh_signal: Notify,
    worker_once: Once,
    tuning: RefreshTuning,
    dashboard_tx: broadcast::Sender<CachedSnapshot>,
}

struct RefreshState {
    dashboard_watchers: usize,
    last_api_demand_at: Option<Instant>,
    next_eligible_at: Option<Instant>,
}

#[derive(Clone, Debug, Default)]
struct LiveCacheState {
    snapshot: CachedLiveSnapshot,
    index_by_sim: HashMap<String, usize>,
    started_at: Option<Instant>,
    total_events: usize,
}

#[derive(Clone, Copy, Debug, Default, Deserialize)]
struct SimsQuery {
    limit: Option<usize>,
}

#[derive(Clone, Debug, Default, Deserialize)]
struct BreakpointTotalsQuery {
    prefix: String,
}

#[derive(Clone, Copy)]
struct RefreshTuning {
    min_refresh_interval: Duration,
    max_refresh_interval: Duration,
    refresh_multiplier: u32,
    api_demand_ttl: Duration,
}

impl Default for RefreshTuning {
    fn default() -> Self {
        Self {
            min_refresh_interval: Duration::from_millis(250),
            max_refresh_interval: Duration::from_secs(5),
            refresh_multiplier: 3,
            api_demand_ttl: Duration::from_secs(10),
        }
    }
}

impl Default for AppState {
    fn default() -> Self {
        Self::new()
    }
}

impl AppState {
    pub fn new() -> Self {
        Self::new_with_tuning(RefreshTuning::default())
    }

    fn new_with_tuning(tuning: RefreshTuning) -> Self {
        let (dashboard_tx, _) = broadcast::channel(32);
        Self {
            inner: Arc::new(AppStateInner {
                registry: Registry::default(),
                live_snapshot: RwLock::new(LiveCacheState::default()),
                analytics_snapshot: RwLock::new(CachedAnalyticsSnapshot::default()),
                refresh: Mutex::new(RefreshState {
                    dashboard_watchers: 0,
                    last_api_demand_at: None,
                    next_eligible_at: None,
                }),
                analytics_dirty: AtomicBool::new(false),
                last_snapshot_copy_micros: std::sync::atomic::AtomicU64::new(0),
                last_snapshot_compute_micros: std::sync::atomic::AtomicU64::new(0),
                refresh_signal: Notify::new(),
                worker_once: Once::new(),
                tuning,
                dashboard_tx,
            }),
        }
    }

    /// Convenience entry point used by tests and `AppState::apply_event`.
    /// Hot-path ingest bypasses the registry lookup via `apply_event_with_handle`.
    pub fn apply_event(&self, envelope: EventEnvelope) -> Result<(), String> {
        let handle = self.inner.registry.get_or_create(&envelope.sim_id);
        self.apply_event_with_handle(&handle, envelope)
    }

    /// Apply an event using a pre-cached sim handle.  No registry access.
    pub(crate) fn apply_event_with_handle(
        &self,
        handle: &Arc<SimHandle>,
        envelope: EventEnvelope,
    ) -> Result<(), String> {
        if handle.sim_id != envelope.sim_id {
            return Err(format!(
                "sim_id mismatch for cached handle: expected {}, got {}",
                handle.sim_id, envelope.sim_id
            ));
        }
        let analytics_affected = event_affects_analytics(&envelope);
        let outcome = handle.apply_event(&envelope)?;
        self.apply_live_event(&envelope, &outcome);

        if self.inner.dashboard_tx.receiver_count() > 0 {
            let _ = self.inner.dashboard_tx.send(self.current_snapshot());
        }

        if analytics_affected {
            self.mark_analytics_stale();
            self.inner.analytics_dirty.store(true, Ordering::SeqCst);
            self.ensure_worker();
            self.signal_refresh();
        }

        Ok(())
    }

    /// Build a full snapshot by reading all sim handles and global atomics.
    /// Live state comes from the cheap published live cache; analytics comes
    /// from the detached published analytics snapshot.
    pub fn current_snapshot(&self) -> CachedSnapshot {
        let live_snapshot = self
            .inner
            .live_snapshot
            .read()
            .expect("live snapshot lock poisoned")
            .snapshot
            .clone();

        let mut analytics_snapshot = self
            .inner
            .analytics_snapshot
            .read()
            .expect("analytics snapshot lock poisoned")
            .clone();
        analytics_snapshot.analytics_meta.age_seconds =
            analytics_age_seconds(analytics_snapshot.analytics_meta.computed_at_ms);

        CachedSnapshot {
            summary: SummaryResponse {
                live_summary: live_snapshot.live_summary,
                analytics_summary: analytics_snapshot.analytics_summary,
                analytics_meta: analytics_snapshot.analytics_meta,
            },
            sims: live_snapshot.sims,
        }
    }

    fn current_summary(&self) -> SummaryResponse {
        let live_summary = self
            .inner
            .live_snapshot
            .read()
            .expect("live snapshot lock poisoned")
            .snapshot
            .live_summary
            .clone();

        let mut analytics_snapshot = self
            .inner
            .analytics_snapshot
            .read()
            .expect("analytics snapshot lock poisoned")
            .clone();
        analytics_snapshot.analytics_meta.age_seconds =
            analytics_age_seconds(analytics_snapshot.analytics_meta.computed_at_ms);

        SummaryResponse {
            live_summary,
            analytics_summary: analytics_snapshot.analytics_summary,
            analytics_meta: analytics_snapshot.analytics_meta,
        }
    }

    fn current_sims(&self, limit: usize) -> Vec<SimSummaryResponse> {
        self.inner
            .live_snapshot
            .read()
            .expect("live snapshot lock poisoned")
            .snapshot
            .sims
            .iter()
            .take(limit)
            .cloned()
            .collect()
    }

    fn current_breakpoint_totals(&self, prefix: &str) -> BreakpointTotalsResponse {
        self.inner
            .live_snapshot
            .read()
            .expect("live snapshot lock poisoned")
            .snapshot
            .sims
            .iter()
            .filter(|sim| sim.sim_id.starts_with(prefix))
            .fold(
                BreakpointTotalsResponse::default(),
                |mut totals, sim| {
                    totals.connected_sims += 1;
                    totals.pickup_count += sim.pickup_count;
                    totals.drop_count += sim.drop_count;
                    totals.turn_move_count += sim.turn_move_count;
                    totals.loose_food_count += sim.loose_food_count;
                    totals
                },
            )
    }

    fn apply_live_event(&self, envelope: &EventEnvelope, outcome: &crate::store::EventOutcome) {
        let mut live_cache = self
            .inner
            .live_snapshot
            .write()
            .expect("live snapshot lock poisoned");

        let sim = live_cache.sim_summary_mut(&envelope.sim_id);
        sim.ant_count = outcome.ant_count;
        sim.pickup_count = outcome.pickup_count;
        sim.drop_count = outcome.drop_count;
        sim.turn_move_count = outcome.turn_move_count;
        sim.loose_food_count = outcome.sim_loose_food_count;

        live_cache.total_events += 1;
        if live_cache.started_at.is_none() {
            live_cache.started_at = Some(Instant::now());
        }
        live_cache.snapshot.live_summary.connected_sim_count = live_cache.snapshot.sims.len();
        live_cache.snapshot.live_summary.loose_food_count = live_cache
            .snapshot
            .live_summary
            .loose_food_count
            .saturating_add_signed(outcome.loose_food_delta);
        let (elapsed_seconds, events_per_second) = live_cache.metrics();
        live_cache.snapshot.live_summary.elapsed_seconds = elapsed_seconds;
        live_cache.snapshot.live_summary.events_per_second = events_per_second;
    }

    fn remove_live_sim(&self, sim_id: &str, removed: &SimSummaryResponse) {
        let mut live_cache = self
            .inner
            .live_snapshot
            .write()
            .expect("live snapshot lock poisoned");

        if let Some(index) = live_cache.index_by_sim.remove(sim_id) {
            live_cache.snapshot.sims.remove(index);
            live_cache.index_by_sim.clear();
            let sim_ids: Vec<String> = live_cache
                .snapshot
                .sims
                .iter()
                .map(|sim| sim.sim_id.clone())
                .collect();
            for (new_index, sim_id) in sim_ids.into_iter().enumerate() {
                live_cache.index_by_sim.insert(sim_id, new_index);
            }
            live_cache.snapshot.live_summary.connected_sim_count = live_cache.snapshot.sims.len();
            live_cache.snapshot.live_summary.loose_food_count = live_cache
                .snapshot
                .live_summary
                .loose_food_count
                .saturating_sub(removed.loose_food_count);
            let (elapsed_seconds, events_per_second) = live_cache.metrics();
            live_cache.snapshot.live_summary.elapsed_seconds = elapsed_seconds;
            live_cache.snapshot.live_summary.events_per_second = events_per_second;
        }
    }

    fn remove_sim_handle(&self, handle: &Arc<SimHandle>) {
        let removed = handle.sim_summary();
        if self
            .inner
            .registry
            .remove_if_same_handle(&handle.sim_id, handle)
        {
            self.remove_live_sim(&handle.sim_id, &removed);
            if self.inner.dashboard_tx.receiver_count() > 0 {
                let _ = self.inner.dashboard_tx.send(self.current_snapshot());
            }
        }
    }

    async fn register_api_demand(&self) {
        let mut refresh = self.inner.refresh.lock().await;
        refresh.last_api_demand_at = Some(Instant::now());
        drop(refresh);
        self.ensure_worker();
        self.signal_refresh();
    }

    async fn add_dashboard_watcher(&self) -> broadcast::Receiver<CachedSnapshot> {
        let receiver = self.inner.dashboard_tx.subscribe();
        let mut refresh = self.inner.refresh.lock().await;
        refresh.dashboard_watchers += 1;
        drop(refresh);
        self.ensure_worker();
        self.signal_refresh();
        receiver
    }

    async fn remove_dashboard_watcher(&self) {
        let mut refresh = self.inner.refresh.lock().await;
        refresh.dashboard_watchers = refresh.dashboard_watchers.saturating_sub(1);
    }

    fn spawn_refresh(&self) {
        let state = self.clone();
        tokio::spawn(async move {
            state.run_refresh_worker().await;
        });
    }

    fn ensure_worker(&self) {
        let state = self.clone();
        self.inner.worker_once.call_once(move || {
            state.spawn_refresh();
        });
    }

    fn signal_refresh(&self) {
        self.inner.refresh_signal.notify_one();
    }

    fn mark_analytics_stale(&self) {
        let mut analytics_snapshot = self
            .inner
            .analytics_snapshot
            .write()
            .expect("analytics snapshot lock poisoned");
        analytics_snapshot.analytics_meta.is_stale = true;
        analytics_snapshot.analytics_meta.age_seconds =
            analytics_age_seconds(analytics_snapshot.analytics_meta.computed_at_ms);
    }

    async fn run_refresh_worker(self) {
        loop {
            self.inner.refresh_signal.notified().await;
            loop {
                let now = Instant::now();
                let plan = self.next_refresh_plan(now).await;
                let Some(delay) = plan else {
                    break;
                };

                if !delay.is_zero() {
                    tokio::select! {
                        _ = tokio::time::sleep(delay) => {}
                        _ = self.inner.refresh_signal.notified() => {
                            continue;
                        }
                    }
                }

                if !self.inner.analytics_dirty.swap(false, Ordering::SeqCst) {
                    continue;
                }

                // Gather analytics input: briefly clone handles, then scan
                // their atomic food slots with no lock held.
                let copy_started = Instant::now();
                let analytics_input = self.inner.registry.analytics_input_data();
                self.inner
                    .last_snapshot_copy_micros
                    .store(copy_started.elapsed().as_micros() as u64, Ordering::SeqCst);

                let compute_started = Instant::now();
                let analytics_summary =
                    tokio::task::spawn_blocking(move || analytics_input.summary())
                        .await
                        .expect("analytics compute task should join");
                let compute_duration = compute_started.elapsed();
                self.inner
                    .last_snapshot_compute_micros
                    .store(compute_duration.as_micros() as u64, Ordering::SeqCst);

                {
                    let stale_again = self.inner.analytics_dirty.load(Ordering::SeqCst);
                    let mut analytics_guard = self
                        .inner
                        .analytics_snapshot
                        .write()
                        .expect("analytics snapshot lock poisoned");
                    *analytics_guard = CachedAnalyticsSnapshot {
                        analytics_summary,
                        analytics_meta: AnalyticsMetaResponse {
                            computed_at_ms: unix_timestamp_ms(),
                            age_seconds: 0.0,
                            is_stale: stale_again,
                        },
                    };
                }
                let _ = self.inner.dashboard_tx.send(self.current_snapshot());
                let mut refresh = self.inner.refresh.lock().await;
                refresh.next_eligible_at =
                    Some(Instant::now() + self.adaptive_refresh_interval(compute_duration));
            }
        }
    }

    async fn next_refresh_plan(&self, now: Instant) -> Option<Duration> {
        let refresh = self.inner.refresh.lock().await;
        if !self.has_demand_locked(&refresh, now)
            || !self.inner.analytics_dirty.load(Ordering::SeqCst)
        {
            return None;
        }
        match refresh.next_eligible_at {
            Some(next) if next > now => Some(next.duration_since(now)),
            _ => Some(Duration::ZERO),
        }
    }

    fn has_demand_locked(&self, refresh: &RefreshState, now: Instant) -> bool {
        if refresh.dashboard_watchers > 0 {
            return true;
        }
        refresh
            .last_api_demand_at
            .map(|at| now.duration_since(at) <= self.inner.tuning.api_demand_ttl)
            .unwrap_or(false)
    }

    fn adaptive_refresh_interval(&self, compute_duration: Duration) -> Duration {
        let mut interval =
            compute_duration.saturating_mul(self.inner.tuning.refresh_multiplier);
        if interval < self.inner.tuning.min_refresh_interval {
            interval = self.inner.tuning.min_refresh_interval;
        }
        if interval > self.inner.tuning.max_refresh_interval {
            interval = self.inner.tuning.max_refresh_interval;
        }
        interval
    }
}

impl LiveCacheState {
    fn sim_summary_mut(&mut self, sim_id: &str) -> &mut SimSummaryResponse {
        if let Some(index) = self.index_by_sim.get(sim_id).copied() {
            return &mut self.snapshot.sims[index];
        }

        let index = self.snapshot.sims.len();
        self.snapshot.sims.push(SimSummaryResponse {
            sim_id: sim_id.to_string(),
            ..SimSummaryResponse::default()
        });
        self.index_by_sim.insert(sim_id.to_string(), index);
        &mut self.snapshot.sims[index]
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
}

fn event_affects_analytics(envelope: &EventEnvelope) -> bool {
    matches!(
        envelope.payload,
        EventPayload::SimFoodSnapshot(_)
            | EventPayload::FoodPickup(_)
            | EventPayload::FoodDrop(_)
    )
}

fn unix_timestamp_ms() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .expect("system time before unix epoch")
        .as_millis() as i64
}

fn analytics_age_seconds(computed_at_ms: i64) -> f64 {
    if computed_at_ms <= 0 {
        return 0.0;
    }
    let now_ms = unix_timestamp_ms();
    if now_ms <= computed_at_ms {
        return 0.0;
    }
    (now_ms - computed_at_ms) as f64 / 1000.0
}

pub fn build_router() -> Router {
    build_router_with_state(AppState::new())
}

pub async fn serve(addr: &str) -> std::io::Result<()> {
    let listener = TcpListener::bind(addr).await?;
    axum::serve(listener, build_router()).await
}

pub fn build_router_with_state(state: AppState) -> Router {
    Router::new()
        .route("/healthz", get(healthz))
        .route("/api/summary", get(summary))
        .route("/api/sims", get(sims))
        .route("/api/breakpoint_totals", get(breakpoint_totals))
        .route("/ws/ingest", get(ingest_ws))
        .route("/ws/dashboard", get(dashboard_ws))
        .route("/", get(dashboard))
        .with_state(state)
}

async fn healthz() -> impl IntoResponse {
    "ok"
}

async fn summary(State(state): State<AppState>) -> Json<SummaryResponse> {
    let summary = state.current_summary();
    state.register_api_demand().await;
    Json(summary)
}

async fn sims(
    State(state): State<AppState>,
    Query(query): Query<SimsQuery>,
) -> Json<Vec<SimSummaryResponse>> {
    let limit = match query.limit {
        Some(0) => usize::MAX,
        Some(limit) => limit,
        None => 20,
    };
    let sims = state.current_sims(limit);
    state.register_api_demand().await;
    Json(sims)
}

async fn breakpoint_totals(
    State(state): State<AppState>,
    Query(query): Query<BreakpointTotalsQuery>,
) -> Json<BreakpointTotalsResponse> {
    Json(state.current_breakpoint_totals(&query.prefix))
}

async fn dashboard(State(state): State<AppState>) -> Html<String> {
    Html(render_dashboard(&state.current_snapshot()))
}

async fn ingest_ws(ws: WebSocketUpgrade, State(state): State<AppState>) -> impl IntoResponse {
    ws.on_upgrade(move |socket| handle_ingest_socket(state, socket))
}

async fn dashboard_ws(ws: WebSocketUpgrade, State(state): State<AppState>) -> impl IntoResponse {
    ws.on_upgrade(move |socket| handle_dashboard_socket(state, socket))
}

/// Ingest WebSocket handler.
/// The sim handle is looked up once from the registry (on first event) and
/// cached for the lifetime of the connection.  All subsequent events bypass
/// the registry entirely.
async fn handle_ingest_socket(state: AppState, mut socket: WebSocket) {
    let mut cached_handle: Option<Arc<SimHandle>> = None;
    while let Some(message_result) = socket.next().await {
        let Ok(message) = message_result else {
            break;
        };
        let Message::Text(text) = message else {
            continue;
        };
        let Ok(event) = serde_json::from_str::<EventEnvelope>(&text) else {
            continue;
        };
        let handle = cached_handle
            .get_or_insert_with(|| state.inner.registry.get_or_create(&event.sim_id));
        if state.apply_event_with_handle(handle, event).is_err() {
            break;
        }
    }
    if let Some(handle) = cached_handle {
        state.remove_sim_handle(&handle);
    }
}

async fn handle_dashboard_socket(state: AppState, mut socket: WebSocket) {
    let initial = state.current_snapshot();
    if socket
        .send(Message::Text(
            serde_json::to_string(&initial)
                .expect("dashboard snapshot json")
                .into(),
        ))
        .await
        .is_err()
    {
        return;
    }

    let mut receiver = state.add_dashboard_watcher().await;
    loop {
        match receiver.recv().await {
            Ok(snapshot) => {
                if socket
                    .send(Message::Text(
                        serde_json::to_string(&snapshot)
                            .expect("dashboard snapshot json")
                            .into(),
                    ))
                    .await
                    .is_err()
                {
                    break;
                }
            }
            Err(broadcast::error::RecvError::Lagged(_)) => continue,
            Err(broadcast::error::RecvError::Closed) => break,
        }
    }
    state.remove_dashboard_watcher().await;
}

#[cfg(test)]
mod tests {
    use std::{
        sync::Arc,
        sync::atomic::Ordering,
        time::{Duration, Instant},
    };

    use serde_json::Value;

    use super::{AppState, RefreshTuning};
    use crate::protocol::{
        EventEnvelope, EventPayload, FoodSnapshotPayload, HelloPayload, StartupFoodPayload,
    };

    // ---- helpers ----

    fn sim_hello_envelope(sim_id: &str) -> EventEnvelope {
        EventEnvelope {
            event_type: "sim_hello".into(),
            sim_id: sim_id.into(),
            seq: 1,
            timestamp_ms: 0,
            payload: EventPayload::SimHello(HelloPayload {
                sim_name: sim_id.into(),
                source: "test".into(),
                session_started_ms: 0,
                world_width: 1280.0,
                world_height: 720.0,
                ant_count: 26,
                food_count: 2,
            }),
        }
    }

    fn sim_food_snapshot_envelope(sim_id: &str, count: usize) -> EventEnvelope {
        EventEnvelope {
            event_type: "sim_food_snapshot".into(),
            sim_id: sim_id.into(),
            seq: 2,
            timestamp_ms: 0,
            payload: EventPayload::SimFoodSnapshot(FoodSnapshotPayload {
                foods: (0..count)
                    .map(|i| StartupFoodPayload {
                        food_id: i,
                        x: (i * 10) as f32,
                        y: (i * 10) as f32,
                    })
                    .collect(),
            }),
        }
    }

    fn food_pickup_envelope(sim_id: &str, slot: usize, seq: u64) -> EventEnvelope {
        EventEnvelope {
            event_type: "food_pickup".into(),
            sim_id: sim_id.into(),
            seq,
            timestamp_ms: 0,
            payload: EventPayload::FoodPickup(crate::protocol::FoodPickupPayload {
                ant_id: Some("ant-1".into()),
                food_id: slot,
                x: None,
                y: None,
                direction_x: None,
                direction_y: None,
                frame: None,
            }),
        }
    }

    fn food_drop_envelope(sim_id: &str, slot: usize, x: f32, y: f32, seq: u64) -> EventEnvelope {
        EventEnvelope {
            event_type: "food_drop".into(),
            sim_id: sim_id.into(),
            seq,
            timestamp_ms: 0,
            payload: EventPayload::FoodDrop(crate::protocol::FoodDropPayload {
                ant_id: Some("ant-1".into()),
                food_id: slot,
                x,
                y,
                direction_x: Some(0.0),
                direction_y: Some(1.0),
                frame: Some(seq),
            }),
        }
    }

    fn current_snapshot_json(state: &AppState) -> Value {
        serde_json::to_value(state.current_snapshot()).expect("snapshot json")
    }

    async fn wait_for_live_loose_food_count(state: &AppState, expected: usize) {
        let deadline = tokio::time::Instant::now() + Duration::from_millis(250);
        loop {
            let snapshot = current_snapshot_json(state);
            if snapshot["summary"]["live_summary"]["loose_food_count"] == expected {
                return;
            }
            assert!(
                tokio::time::Instant::now() < deadline,
                "expected live loose_food_count={expected}, last snapshot was {snapshot:?}"
            );
            tokio::time::sleep(Duration::from_millis(10)).await;
        }
    }

    async fn wait_for_analytics_occupied_cells(state: &AppState, expected: usize) {
        let deadline = tokio::time::Instant::now() + Duration::from_millis(250);
        loop {
            let snapshot = current_snapshot_json(state);
            if snapshot["summary"]["analytics_summary"]["occupied_cell_count"] == expected {
                return;
            }
            assert!(
                tokio::time::Instant::now() < deadline,
                "expected analytics occupied_cell_count={expected}, last snapshot was {snapshot:?}"
            );
            tokio::time::sleep(Duration::from_millis(10)).await;
        }
    }

    // ---- correctness tests ----

    #[tokio::test]
    async fn duplicate_food_drop_does_not_inflate_live_loose_food_count() {
        let state = AppState::new();
        let sid = "sim-dup";
        state.apply_event(sim_hello_envelope(sid)).expect("sim_hello");
        state
            .apply_event(sim_food_snapshot_envelope(sid, 1))
            .expect("snapshot");
        state
            .apply_event(food_drop_envelope(sid, 0, 10.0, 20.0, 3))
            .expect("first food_drop");
        state
            .apply_event(food_drop_envelope(sid, 0, 30.0, 40.0, 4))
            .expect("duplicate food_drop");

        let snapshot = current_snapshot_json(&state);
        assert_eq!(
            snapshot["summary"]["live_summary"]["loose_food_count"],
            1,
            "duplicate food_drop with same food_id should not inflate live loose_food_count"
        );
        assert_eq!(
            snapshot["sims"][0]["loose_food_count"],
            1,
            "per-sim loose_food_count should match after duplicate food_drop"
        );
    }

    #[tokio::test]
    async fn pickup_of_missing_food_does_not_deflate_live_loose_food_count() {
        let state = AppState::new();
        let sid = "sim-miss";
        state.apply_event(sim_hello_envelope(sid)).expect("sim_hello");
        state
            .apply_event(sim_food_snapshot_envelope(sid, 1))
            .expect("snapshot");
        state
            .apply_event(EventEnvelope {
                event_type: "food_pickup".into(),
                sim_id: sid.into(),
                seq: 3,
                timestamp_ms: 0,
                payload: EventPayload::FoodPickup(crate::protocol::FoodPickupPayload {
                    ant_id: Some("ant-1".into()),
                    food_id: usize::MAX,
                    x: Some(50.0),
                    y: Some(50.0),
                    direction_x: Some(1.0),
                    direction_y: Some(0.0),
                    frame: Some(2),
                }),
            })
            .expect("pickup of missing food");

        let snapshot = current_snapshot_json(&state);
        assert_eq!(
            snapshot["summary"]["live_summary"]["loose_food_count"],
            1,
            "pickup of non-existent food should not deflate live loose_food_count"
        );
        assert_eq!(
            snapshot["sims"][0]["loose_food_count"],
            1,
            "per-sim loose_food_count should match after pickup of missing food"
        );
    }

    #[test]
    fn watched_live_event_does_not_rebuild_all_sim_snapshots() {
        let state = AppState::new();
        let _watcher = state.inner.dashboard_tx.subscribe();
        let before = state.inner.registry.all_handles_calls_for_test();

        state
            .apply_event(sim_hello_envelope("sim-no-scan"))
            .expect("sim_hello should be accepted");

        let after = state.inner.registry.all_handles_calls_for_test();
        assert_eq!(
            after, before,
            "live event path should not iterate all sim handles just to publish immediate live state"
        );
    }

    #[test]
    fn apply_event_with_handle_rejects_sim_id_mismatch() {
        let state = AppState::new();
        let handle = state.inner.registry.get_or_create("sim-a");

        state
            .apply_event_with_handle(&handle, sim_hello_envelope("sim-a"))
            .expect("initial sim-a event should be accepted");

        let error = state
            .apply_event_with_handle(&handle, sim_hello_envelope("sim-b"))
            .expect_err("cached handle should reject later events for a different sim_id");

        assert!(
            error.contains("sim_id"),
            "expected sim_id mismatch error, got {error}"
        );

        let snapshot = current_snapshot_json(&state);
        let sims = snapshot["sims"].as_array().expect("sims array");
        assert_eq!(sims.len(), 1);
        assert_eq!(sims[0]["sim_id"], "sim-a");
    }

    // ---- analytics scheduling tests ----

    #[tokio::test]
    async fn api_demand_expires_before_later_dirty_events_refresh_snapshot() {
        let state = AppState::new_with_tuning(RefreshTuning {
            min_refresh_interval: Duration::ZERO,
            max_refresh_interval: Duration::ZERO,
            refresh_multiplier: 1,
            api_demand_ttl: Duration::from_millis(30),
        });

        let sid = "sim-demand-expiry";
        state.apply_event(sim_hello_envelope(sid)).expect("sim hello");
        state
            .apply_event(sim_food_snapshot_envelope(sid, 2))
            .expect("snapshot");
        state.apply_event(food_pickup_envelope(sid, 0, 3)).expect("pickup 0");
        state.apply_event(food_pickup_envelope(sid, 1, 4)).expect("pickup 1");
        state
            .apply_event(food_drop_envelope(sid, 0, 1.0, 1.0, 5))
            .expect("food drop");

        state.register_api_demand().await;
        wait_for_live_loose_food_count(&state, 1).await;
        wait_for_analytics_occupied_cells(&state, 1).await;

        tokio::time::sleep(Duration::from_millis(50)).await;

        state
            .apply_event(food_drop_envelope(sid, 1, 2.0, 2.0, 6))
            .expect("second food drop");

        tokio::time::sleep(Duration::from_millis(20)).await;
        let stale_snapshot = current_snapshot_json(&state);
        assert_eq!(stale_snapshot["summary"]["live_summary"]["loose_food_count"], 2);
        assert_eq!(
            stale_snapshot["summary"]["analytics_summary"]["occupied_cell_count"],
            1
        );
        assert_eq!(stale_snapshot["summary"]["analytics_meta"]["is_stale"], true);

        state.register_api_demand().await;
        wait_for_live_loose_food_count(&state, 2).await;
        wait_for_analytics_occupied_cells(&state, 1).await;
    }

    #[tokio::test]
    async fn active_demand_refreshes_are_cadence_limited() {
        let state = AppState::new_with_tuning(RefreshTuning {
            min_refresh_interval: Duration::from_millis(60),
            max_refresh_interval: Duration::from_millis(60),
            refresh_multiplier: 1,
            api_demand_ttl: Duration::from_secs(1),
        });

        let sid = "sim-cadence";
        state.apply_event(sim_hello_envelope(sid)).expect("sim hello");
        state
            .apply_event(sim_food_snapshot_envelope(sid, 2))
            .expect("snapshot");
        state.apply_event(food_pickup_envelope(sid, 0, 3)).expect("pickup 0");
        state.apply_event(food_pickup_envelope(sid, 1, 4)).expect("pickup 1");
        state
            .apply_event(food_drop_envelope(sid, 0, 1.0, 1.0, 5))
            .expect("initial food drop");

        state.register_api_demand().await;
        wait_for_live_loose_food_count(&state, 1).await;
        wait_for_analytics_occupied_cells(&state, 1).await;

        state
            .apply_event(food_drop_envelope(sid, 1, 2.0, 2.0, 6))
            .expect("second food drop");

        tokio::time::sleep(Duration::from_millis(20)).await;
        let stale_snapshot = current_snapshot_json(&state);
        assert_eq!(stale_snapshot["summary"]["live_summary"]["loose_food_count"], 2);
        assert_eq!(
            stale_snapshot["summary"]["analytics_summary"]["occupied_cell_count"],
            1
        );
        assert_eq!(stale_snapshot["summary"]["analytics_meta"]["is_stale"], true);

        wait_for_live_loose_food_count(&state, 2).await;
        wait_for_analytics_occupied_cells(&state, 1).await;
    }

    // ---- concurrency tests ----

    /// Verify that the analytics worker does not block ingest writes.
    /// With atomic food slots, analytics reads and ingest writes are fully
    /// independent -- this test documents that invariant.
    #[tokio::test(flavor = "multi_thread", worker_threads = 2)]
    async fn analytics_refresh_does_not_block_ingest_writes() {
        let state = AppState::new();
        let sid = "sim-noblock";
        state.apply_event(sim_hello_envelope(sid)).expect("sim hello");
        state
            .apply_event(EventEnvelope {
                event_type: "sim_food_snapshot".into(),
                sim_id: sid.into(),
                seq: 2,
                timestamp_ms: 0,
                payload: EventPayload::SimFoodSnapshot(FoodSnapshotPayload {
                    foods: (0..5000)
                        .map(|index| StartupFoodPayload {
                            food_id: index,
                            x: ((index * 17) % 4000) as f32,
                            y: ((index * 19) % 4000) as f32,
                        })
                        .collect(),
                }),
            })
            .expect("large snapshot");

        state.register_api_demand().await;

        // While analytics is computing (spawn_blocking with O(N^2) work),
        // ingest writes should complete immediately since they only do atomic ops.
        let write_times: Vec<Duration> = (0..20)
            .map(|i| {
                let state = state.clone();
                let sid = sid.to_string();
                let start = Instant::now();
                state
                    .apply_event(food_pickup_envelope(&sid, i % 5000, i as u64 + 10))
                    .expect("pickup should succeed");
                start.elapsed()
            })
            .collect();

        let max_write_time = write_times.iter().max().copied().unwrap_or_default();
        assert!(
            max_write_time < Duration::from_millis(50),
            "ingest writes should complete quickly regardless of analytics, max was {:?}",
            max_write_time
        );
    }

    /// Verify that two sims with different handles can write fully concurrently.
    #[tokio::test(flavor = "multi_thread", worker_threads = 2)]
    async fn concurrent_writes_to_different_sims_do_not_block_each_other() {
        let state = AppState::new();

        for sid in ["sim-x", "sim-y"] {
            state.apply_event(sim_hello_envelope(sid)).expect("hello");
            state
                .apply_event(sim_food_snapshot_envelope(sid, 10))
                .expect("snapshot");
        }

        let wrote_x = Arc::new(std::sync::atomic::AtomicBool::new(false));
        let wrote_y = Arc::new(std::sync::atomic::AtomicBool::new(false));

        let state_x = state.clone();
        let flag_x = wrote_x.clone();
        let writer_x = std::thread::spawn(move || {
            for i in 0..100 {
                state_x
                    .apply_event(food_pickup_envelope("sim-x", i % 10, i as u64 + 10))
                    .expect("sim-x write");
            }
            flag_x.store(true, Ordering::SeqCst);
        });

        let state_y = state.clone();
        let flag_y = wrote_y.clone();
        let writer_y = std::thread::spawn(move || {
            for i in 0..100 {
                state_y
                    .apply_event(food_pickup_envelope("sim-y", i % 10, i as u64 + 10))
                    .expect("sim-y write");
            }
            flag_y.store(true, Ordering::SeqCst);
        });

        writer_x.join().expect("sim-x writer should join");
        writer_y.join().expect("sim-y writer should join");

        assert!(wrote_x.load(Ordering::SeqCst));
        assert!(wrote_y.load(Ordering::SeqCst));

        let snapshot = current_snapshot_json(&state);
        assert_eq!(snapshot["summary"]["live_summary"]["connected_sim_count"], 2);
    }

    /// Verify the refresh coordination mutex does not block unrelated async tasks.
    #[tokio::test(flavor = "multi_thread", worker_threads = 1)]
    async fn refresh_coordination_contention_does_not_block_runtime_progress() {
        let state = AppState::new();
        let refresh_guard = state.inner.refresh.lock().await;

        let contender = {
            let state = state.clone();
            tokio::spawn(async move {
                state.register_api_demand().await;
            })
        };

        tokio::task::yield_now().await;

        let ticked = tokio::spawn(async move {
            tokio::time::sleep(Duration::from_millis(20)).await;
        });

        let runtime_made_progress = tokio::time::timeout(Duration::from_millis(60), ticked)
            .await
            .is_ok();
        drop(refresh_guard);
        contender.await.expect("contender task should join");

        assert!(
            runtime_made_progress,
            "refresh coordination contention should not block unrelated async progress"
        );
    }

    /// Verify that heavy analytics compute in spawn_blocking does not starve runtime.
    #[test]
    fn heavy_refresh_compute_does_not_starve_runtime_progress() {
        let (tick_tx, tick_rx) = std::sync::mpsc::channel();

        let runtime_thread = std::thread::spawn(move || {
            let runtime = tokio::runtime::Builder::new_multi_thread()
                .worker_threads(1)
                .enable_time()
                .build()
                .expect("test runtime should build");
            runtime.block_on(async move {
                let state = AppState::new();
                state.apply_event(sim_hello_envelope("sim-cpu")).expect("hello");
                state
                    .apply_event(EventEnvelope {
                        event_type: "sim_food_snapshot".into(),
                        sim_id: "sim-cpu".into(),
                        seq: 2,
                        timestamp_ms: 0,
                        payload: EventPayload::SimFoodSnapshot(FoodSnapshotPayload {
                            foods: (0..5000)
                                .map(|index| StartupFoodPayload {
                                    food_id: index,
                                    x: ((index * 17) % 4000) as f32,
                                    y: ((index * 19) % 4000) as f32,
                                })
                                .collect(),
                        }),
                    })
                    .expect("food snapshot");

                state.register_api_demand().await;
                tokio::task::yield_now().await;

                tokio::spawn(async move {
                    tokio::time::sleep(Duration::from_millis(20)).await;
                    let _ = tick_tx.send(());
                });

                tokio::time::sleep(Duration::from_millis(100)).await;
            });
        });

        let runtime_made_progress = tick_rx.recv_timeout(Duration::from_millis(60)).is_ok();
        runtime_thread
            .join()
            .expect("runtime thread should join after heavy refresh test");

        assert!(
            runtime_made_progress,
            "heavy refresh compute should not starve unrelated runtime progress"
        );
    }
}
