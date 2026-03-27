use std::sync::{
    Arc, Once, RwLock,
    atomic::{AtomicBool, Ordering},
};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use axum::{
    Json, Router,
    extract::{
        State,
        ws::{Message, WebSocket, WebSocketUpgrade},
    },
    response::{Html, IntoResponse},
    routing::get,
};
use futures_util::StreamExt;
use tokio::net::TcpListener;
use tokio::sync::{Mutex, Notify, broadcast};

use crate::{
    dashboard::render_dashboard,
    ingest::handle_ingest_event,
    protocol::{EventEnvelope, EventPayload},
    store::{EventOutcome, Store},
    summary::{
        AnalyticsMetaResponse, CachedAnalyticsSnapshot, CachedLiveSnapshot, CachedSnapshot,
        SimSummaryResponse, SummaryResponse,
    },
};

#[derive(Clone)]
pub struct AppState {
    inner: Arc<AppStateInner>,
}

struct AppStateInner {
    store: Store,
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
    started_at: Option<Instant>,
    total_events: usize,
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

impl AppState {
    pub fn new() -> Self {
        Self::new_with_tuning(RefreshTuning::default())
    }

    fn new_with_tuning(tuning: RefreshTuning) -> Self {
        let (dashboard_tx, _) = broadcast::channel(32);
        Self {
            inner: Arc::new(AppStateInner {
                store: Store::default(),
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

    pub fn apply_event(&self, envelope: EventEnvelope) -> Result<(), String> {
        let analytics_affected = event_affects_analytics(&envelope);
        let outcome = self.inner.store.apply_event(&envelope)?;
        self.apply_live_event(&envelope, &outcome);
        let _ = self.inner.dashboard_tx.send(self.current_snapshot());
        if analytics_affected {
            self.mark_analytics_stale();
            self.inner.analytics_dirty.store(true, Ordering::SeqCst);
            self.ensure_worker();
            self.signal_refresh();
        }
        Ok(())
    }

    fn current_snapshot(&self) -> CachedSnapshot {
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

    fn apply_live_event(&self, envelope: &EventEnvelope, outcome: &EventOutcome) {
        let mut live_cache = self
            .inner
            .live_snapshot
            .write()
            .expect("live snapshot lock poisoned");

        let sim = live_cache.sim_summary_mut(&envelope.sim_id);
        let previous_loose_food = sim.loose_food_count;

        match &envelope.payload {
            EventPayload::SimHello(payload) => {
                sim.ant_count = payload.ant_count;
            }
            EventPayload::SimFoodSnapshot(_) => {}
            EventPayload::FoodPickup(_) => {
                sim.pickup_count += 1;
            }
            EventPayload::FoodDrop(_) => {
                sim.drop_count += 1;
            }
            EventPayload::AntTurnMove(_) => {
                sim.turn_move_count += 1;
            }
            EventPayload::SimHeartbeat(_) | EventPayload::SimGoodbye(_) => {}
        }
        sim.loose_food_count = outcome.sim_loose_food_count;
        let loose_food_delta = sim.loose_food_count as isize - previous_loose_food as isize;

        live_cache.total_events += 1;
        if live_cache.started_at.is_none() {
            live_cache.started_at = Some(Instant::now());
        }
        live_cache.snapshot.live_summary.connected_sim_count = live_cache.snapshot.sims.len();
        live_cache.snapshot.live_summary.loose_food_count = live_cache
            .snapshot
            .live_summary
            .loose_food_count
            .saturating_add_signed(loose_food_delta);
        let (elapsed_seconds, events_per_second) = live_cache.metrics();
        live_cache.snapshot.live_summary.elapsed_seconds = elapsed_seconds;
        live_cache.snapshot.live_summary.events_per_second = events_per_second;
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

                let copy_started = Instant::now();
                let analytics_input = self.inner.store.analytics_input_data();
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
        if let Some(index) = self.snapshot.sims.iter().position(|sim| sim.sim_id == sim_id) {
            return &mut self.snapshot.sims[index];
        }
        self.snapshot.sims.push(SimSummaryResponse {
            sim_id: sim_id.to_string(),
            ..SimSummaryResponse::default()
        });
        self.snapshot.sims.last_mut().expect("new sim summary")
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
        .route("/ws/ingest", get(ingest_ws))
        .route("/ws/dashboard", get(dashboard_ws))
        .route("/", get(dashboard))
        .with_state(state)
}

async fn healthz() -> impl IntoResponse {
    "ok"
}

async fn summary(State(state): State<AppState>) -> Json<SummaryResponse> {
    let snapshot = state.current_snapshot();
    state.register_api_demand().await;
    Json(snapshot.summary)
}

async fn sims(State(state): State<AppState>) -> Json<Vec<SimSummaryResponse>> {
    let snapshot = state.current_snapshot();
    state.register_api_demand().await;
    Json(snapshot.sims)
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

async fn handle_ingest_socket(state: AppState, mut socket: WebSocket) {
    while let Some(message_result) = socket.next().await {
        let Ok(message) = message_result else {
            return;
        };
        let Message::Text(text) = message else {
            continue;
        };
        let Ok(event) = serde_json::from_str::<EventEnvelope>(&text) else {
            continue;
        };
        if handle_ingest_event(&state, event).await.is_err() {
            return;
        }
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
        sync::{Arc, atomic::Ordering},
        time::{Duration, Instant},
    };

    use serde_json::Value;

    use super::{AppState, RefreshTuning};
    use crate::protocol::{
        EventEnvelope, EventPayload, FoodSnapshotPayload, HelloPayload, StartupFoodPayload,
    };

    #[tokio::test(flavor = "multi_thread", worker_threads = 2)]
    async fn refresh_does_not_hold_store_lock_long_enough_to_starve_writes() {
        let state = AppState::new();
        state
            .apply_event(EventEnvelope {
                event_type: "sim_hello".into(),
                sim_id: "sim-lock".into(),
                seq: 1,
                timestamp_ms: 0,
                payload: EventPayload::SimHello(HelloPayload {
                    sim_name: "sim-lock".into(),
                    source: "test".into(),
                    session_started_ms: 0,
                    world_width: 1280.0,
                    world_height: 720.0,
                    ant_count: 26,
                    food_count: 5000,
                }),
            })
            .expect("sim_hello should be accepted");
        state
            .apply_event(EventEnvelope {
                event_type: "sim_food_snapshot".into(),
                sim_id: "sim-lock".into(),
                seq: 2,
                timestamp_ms: 0,
                payload: EventPayload::SimFoodSnapshot(FoodSnapshotPayload {
                    foods: (0..5000)
                        .map(|index| StartupFoodPayload {
                            food_id: format!("food-{index:04}"),
                            x: ((index * 17) % 4000) as f32,
                            y: ((index * 19) % 4000) as f32,
                        })
                        .collect(),
                }),
            })
            .expect("sim_food_snapshot should be accepted");

        state.register_api_demand().await;

        let blocked_for = tokio::task::spawn_blocking({
            let state = state.clone();
            move || {
                let deadline = Instant::now() + Duration::from_secs(5);
                let mut blocked_since: Option<Instant> = None;
                loop {
                    match state.inner.store.try_hold_shard_for_test("sim-lock") {
                        Some(guard) => {
                            drop(guard);
                            if let Some(started_at) = blocked_since {
                                return started_at.elapsed();
                            }
                            if Instant::now() >= deadline {
                                return Duration::ZERO;
                            }
                        }
                        None => {
                            blocked_since.get_or_insert_with(Instant::now);
                        }
                    }
                    assert!(
                        Instant::now() < deadline,
                        "expected refresh to briefly block writes and then release the store lock"
                    );
                    std::thread::sleep(Duration::from_millis(1));
                }
            }
        })
        .await
        .expect("lock timing task should join");

        assert!(
            blocked_for < Duration::from_millis(25),
            "expected refresh to release the store lock quickly, but writes were blocked for {:?}",
            blocked_for
        );
    }

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

    #[tokio::test(flavor = "multi_thread", worker_threads = 2)]
    async fn unrelated_sim_writes_do_not_block_on_global_store_contention() {
        let state = AppState::new();
        state
            .apply_event(EventEnvelope {
                event_type: "sim_hello".into(),
                sim_id: "sim-a".into(),
                seq: 1,
                timestamp_ms: 0,
                payload: EventPayload::SimHello(HelloPayload {
                    sim_name: "sim-a".into(),
                    source: "test".into(),
                    session_started_ms: 0,
                    world_width: 1280.0,
                    world_height: 720.0,
                    ant_count: 26,
                    food_count: 1,
                }),
            })
            .expect("sim-a hello should be accepted");
        state
            .apply_event(EventEnvelope {
                event_type: "sim_hello".into(),
                sim_id: "sim-b".into(),
                seq: 1,
                timestamp_ms: 0,
                payload: EventPayload::SimHello(HelloPayload {
                    sim_name: "sim-b".into(),
                    source: "test".into(),
                    session_started_ms: 0,
                    world_width: 1280.0,
                    world_height: 720.0,
                    ant_count: 26,
                    food_count: 1,
                }),
            })
            .expect("sim-b hello should be accepted");

        let blocked_sim = "sim-a".to_string();
        let mut free_sim = "sim-b".to_string();
        while state.inner.store.shard_index_for_test(&blocked_sim)
            == state.inner.store.shard_index_for_test(&free_sim)
        {
            free_sim.push('x');
        }

        let store_guard = state.inner.store.hold_shard_for_test(&blocked_sim);
        let progressed = Arc::new(std::sync::atomic::AtomicBool::new(false));
        let marker = progressed.clone();
        let writer = std::thread::spawn({
            let state = state.clone();
            let free_sim = free_sim.clone();
            move || {
                state
                    .apply_event(EventEnvelope {
                        event_type: "ant_turn_move".into(),
                        sim_id: free_sim,
                        seq: 2,
                        timestamp_ms: 0,
                        payload: EventPayload::AntTurnMove(crate::protocol::TurnMovePayload {
                            ant_id: "ant-1".into(),
                            x: 1.0,
                            y: 1.0,
                            direction_x: 0.0,
                            direction_y: 1.0,
                            frame: 2,
                        }),
                    })
                    .expect("sim-b move should be accepted");
                marker.store(true, Ordering::SeqCst);
            }
        });

        std::thread::sleep(Duration::from_millis(50));
        let write_completed = progressed.load(Ordering::SeqCst);
        drop(store_guard);
        writer.join().expect("writer thread should join");

        assert!(
            write_completed,
            "writing one sim should not block behind unrelated store contention"
        );
    }

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
                state
                    .apply_event(EventEnvelope {
                        event_type: "sim_hello".into(),
                        sim_id: "sim-cpu".into(),
                        seq: 1,
                        timestamp_ms: 0,
                        payload: EventPayload::SimHello(HelloPayload {
                            sim_name: "sim-cpu".into(),
                            source: "test".into(),
                            session_started_ms: 0,
                            world_width: 1280.0,
                            world_height: 720.0,
                            ant_count: 26,
                            food_count: 5000,
                        }),
                    })
                    .expect("sim hello should be accepted");
                state
                    .apply_event(EventEnvelope {
                        event_type: "sim_food_snapshot".into(),
                        sim_id: "sim-cpu".into(),
                        seq: 2,
                        timestamp_ms: 0,
                        payload: EventPayload::SimFoodSnapshot(FoodSnapshotPayload {
                            foods: (0..5000)
                                .map(|index| StartupFoodPayload {
                                    food_id: format!("food-{index:04}"),
                                    x: ((index * 17) % 4000) as f32,
                                    y: ((index * 19) % 4000) as f32,
                                })
                                .collect(),
                        }),
                    })
                    .expect("sim food snapshot should be accepted");

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

    #[tokio::test]
    async fn api_demand_expires_before_later_dirty_events_refresh_snapshot() {
        let state = AppState::new_with_tuning(RefreshTuning {
            min_refresh_interval: Duration::ZERO,
            max_refresh_interval: Duration::ZERO,
            refresh_multiplier: 1,
            api_demand_ttl: Duration::from_millis(30),
        });

        state
            .apply_event(EventEnvelope {
                event_type: "sim_hello".into(),
                sim_id: "sim-demand-expiry".into(),
                seq: 1,
                timestamp_ms: 0,
                payload: EventPayload::SimHello(HelloPayload {
                    sim_name: "sim-demand-expiry".into(),
                    source: "test".into(),
                    session_started_ms: 0,
                    world_width: 1280.0,
                    world_height: 720.0,
                    ant_count: 26,
                    food_count: 1,
                }),
            })
            .expect("sim hello should be accepted");
        state
            .apply_event(EventEnvelope {
                event_type: "food_drop".into(),
                sim_id: "sim-demand-expiry".into(),
                seq: 2,
                timestamp_ms: 0,
                payload: EventPayload::FoodDrop(crate::protocol::FoodDropPayload {
                    ant_id: Some("ant-1".into()),
                    food_id: "food-1".into(),
                    x: 1.0,
                    y: 1.0,
                    direction_x: Some(0.0),
                    direction_y: Some(1.0),
                    frame: Some(2),
                }),
            })
            .expect("food drop should be accepted");

        state.register_api_demand().await;
        wait_for_live_loose_food_count(&state, 1).await;
        wait_for_analytics_occupied_cells(&state, 1).await;

        tokio::time::sleep(Duration::from_millis(50)).await;

        state
            .apply_event(EventEnvelope {
                event_type: "food_drop".into(),
                sim_id: "sim-demand-expiry".into(),
                seq: 3,
                timestamp_ms: 0,
                payload: EventPayload::FoodDrop(crate::protocol::FoodDropPayload {
                    ant_id: Some("ant-1".into()),
                    food_id: "food-2".into(),
                    x: 2.0,
                    y: 2.0,
                    direction_x: Some(0.0),
                    direction_y: Some(1.0),
                    frame: Some(3),
                }),
            })
            .expect("second food drop should be accepted");

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

        state
            .apply_event(EventEnvelope {
                event_type: "sim_hello".into(),
                sim_id: "sim-cadence".into(),
                seq: 1,
                timestamp_ms: 0,
                payload: EventPayload::SimHello(HelloPayload {
                    sim_name: "sim-cadence".into(),
                    source: "test".into(),
                    session_started_ms: 0,
                    world_width: 1280.0,
                    world_height: 720.0,
                    ant_count: 26,
                    food_count: 1,
                }),
            })
            .expect("sim hello should be accepted");
        state
            .apply_event(EventEnvelope {
                event_type: "food_drop".into(),
                sim_id: "sim-cadence".into(),
                seq: 2,
                timestamp_ms: 0,
                payload: EventPayload::FoodDrop(crate::protocol::FoodDropPayload {
                    ant_id: Some("ant-1".into()),
                    food_id: "food-1".into(),
                    x: 1.0,
                    y: 1.0,
                    direction_x: Some(0.0),
                    direction_y: Some(1.0),
                    frame: Some(2),
                }),
            })
            .expect("initial food drop should be accepted");

        state.register_api_demand().await;
        wait_for_live_loose_food_count(&state, 1).await;
        wait_for_analytics_occupied_cells(&state, 1).await;

        state
            .apply_event(EventEnvelope {
                event_type: "food_drop".into(),
                sim_id: "sim-cadence".into(),
                seq: 3,
                timestamp_ms: 0,
                payload: EventPayload::FoodDrop(crate::protocol::FoodDropPayload {
                    ant_id: Some("ant-1".into()),
                    food_id: "food-2".into(),
                    x: 2.0,
                    y: 2.0,
                    direction_x: Some(0.0),
                    direction_y: Some(1.0),
                    frame: Some(3),
                }),
            })
            .expect("second food drop should be accepted");

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

    fn current_snapshot_json(state: &AppState) -> Value {
        serde_json::to_value(state.current_snapshot()).expect("snapshot json")
    }

    #[tokio::test]
    async fn duplicate_food_drop_does_not_inflate_live_loose_food_count() {
        let state = AppState::new();
        state
            .apply_event(EventEnvelope {
                event_type: "sim_hello".into(),
                sim_id: "sim-dup".into(),
                seq: 1,
                timestamp_ms: 0,
                payload: EventPayload::SimHello(HelloPayload {
                    sim_name: "sim-dup".into(),
                    source: "test".into(),
                    session_started_ms: 0,
                    world_width: 1280.0,
                    world_height: 720.0,
                    ant_count: 1,
                    food_count: 0,
                }),
            })
            .expect("sim_hello");
        state
            .apply_event(EventEnvelope {
                event_type: "food_drop".into(),
                sim_id: "sim-dup".into(),
                seq: 2,
                timestamp_ms: 0,
                payload: EventPayload::FoodDrop(crate::protocol::FoodDropPayload {
                    ant_id: Some("ant-1".into()),
                    food_id: "food-1".into(),
                    x: 10.0,
                    y: 20.0,
                    direction_x: Some(0.0),
                    direction_y: Some(1.0),
                    frame: Some(1),
                }),
            })
            .expect("first food_drop");
        state
            .apply_event(EventEnvelope {
                event_type: "food_drop".into(),
                sim_id: "sim-dup".into(),
                seq: 3,
                timestamp_ms: 0,
                payload: EventPayload::FoodDrop(crate::protocol::FoodDropPayload {
                    ant_id: Some("ant-1".into()),
                    food_id: "food-1".into(),
                    x: 30.0,
                    y: 40.0,
                    direction_x: Some(0.0),
                    direction_y: Some(1.0),
                    frame: Some(2),
                }),
            })
            .expect("duplicate food_drop");

        let snapshot = current_snapshot_json(&state);
        assert_eq!(
            snapshot["summary"]["live_summary"]["loose_food_count"], 1,
            "duplicate food_drop with same food_id should not inflate live loose_food_count"
        );
        assert_eq!(
            snapshot["sims"][0]["loose_food_count"], 1,
            "per-sim loose_food_count should match store truth after duplicate food_drop"
        );
    }

    #[tokio::test]
    async fn pickup_of_missing_food_does_not_deflate_live_loose_food_count() {
        let state = AppState::new();
        state
            .apply_event(EventEnvelope {
                event_type: "sim_hello".into(),
                sim_id: "sim-miss".into(),
                seq: 1,
                timestamp_ms: 0,
                payload: EventPayload::SimHello(HelloPayload {
                    sim_name: "sim-miss".into(),
                    source: "test".into(),
                    session_started_ms: 0,
                    world_width: 1280.0,
                    world_height: 720.0,
                    ant_count: 1,
                    food_count: 0,
                }),
            })
            .expect("sim_hello");
        state
            .apply_event(EventEnvelope {
                event_type: "food_drop".into(),
                sim_id: "sim-miss".into(),
                seq: 2,
                timestamp_ms: 0,
                payload: EventPayload::FoodDrop(crate::protocol::FoodDropPayload {
                    ant_id: Some("ant-1".into()),
                    food_id: "food-1".into(),
                    x: 10.0,
                    y: 20.0,
                    direction_x: Some(0.0),
                    direction_y: Some(1.0),
                    frame: Some(1),
                }),
            })
            .expect("food_drop");
        state
            .apply_event(EventEnvelope {
                event_type: "food_pickup".into(),
                sim_id: "sim-miss".into(),
                seq: 3,
                timestamp_ms: 0,
                payload: EventPayload::FoodPickup(crate::protocol::FoodPickupPayload {
                    ant_id: Some("ant-1".into()),
                    food_id: "food-nonexistent".into(),
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
            snapshot["summary"]["live_summary"]["loose_food_count"], 1,
            "pickup of non-existent food should not deflate live loose_food_count"
        );
        assert_eq!(
            snapshot["sims"][0]["loose_food_count"], 1,
            "per-sim loose_food_count should match store truth after pickup of missing food"
        );
    }
}
