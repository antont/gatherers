use std::sync::{
    Arc, RwLock,
    atomic::{AtomicBool, Ordering},
};
use std::time::Instant;

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
use tokio::sync::{Mutex, broadcast};

use crate::{
    dashboard::render_dashboard,
    ingest::handle_ingest_event,
    protocol::EventEnvelope,
    store::Store,
    summary::{CachedSnapshot, SimSummaryResponse, SummaryResponse},
};

#[derive(Clone)]
pub struct AppState {
    inner: Arc<AppStateInner>,
}

struct AppStateInner {
    store: Store,
    snapshot: RwLock<CachedSnapshot>,
    refresh: Mutex<()>,
    dirty: AtomicBool,
    demand_seen: AtomicBool,
    refresh_in_flight: AtomicBool,
    last_snapshot_copy_micros: std::sync::atomic::AtomicU64,
    last_snapshot_compute_micros: std::sync::atomic::AtomicU64,
    dashboard_tx: broadcast::Sender<CachedSnapshot>,
}

impl AppState {
    pub fn new() -> Self {
        let (dashboard_tx, _) = broadcast::channel(32);
        Self {
            inner: Arc::new(AppStateInner {
                store: Store::default(),
                snapshot: RwLock::new(CachedSnapshot::default()),
                refresh: Mutex::new(()),
                dirty: AtomicBool::new(false),
                demand_seen: AtomicBool::new(false),
                refresh_in_flight: AtomicBool::new(false),
                last_snapshot_copy_micros: std::sync::atomic::AtomicU64::new(0),
                last_snapshot_compute_micros: std::sync::atomic::AtomicU64::new(0),
                dashboard_tx,
            }),
        }
    }

    pub fn apply_event(&self, envelope: EventEnvelope) -> Result<(), String> {
        self.inner
            .store
            .apply_event(&envelope)?;

        self.inner.dirty.store(true, Ordering::SeqCst);
        self.try_spawn_refresh();
        Ok(())
    }

    fn current_snapshot(&self) -> CachedSnapshot {
        self.inner
            .snapshot
            .read()
            .expect("snapshot lock poisoned")
            .clone()
    }

    async fn register_api_demand(&self) {
        let _refresh_gate = self.inner.refresh.lock().await;
        self.inner.demand_seen.store(true, Ordering::SeqCst);
        self.try_spawn_refresh();
    }

    async fn add_dashboard_watcher(&self) -> broadcast::Receiver<CachedSnapshot> {
        let receiver = self.inner.dashboard_tx.subscribe();
        self.register_api_demand().await;
        receiver
    }

    fn spawn_refresh(&self) {
        let state = self.clone();
        tokio::spawn(async move {
            state.inner.dirty.store(false, Ordering::SeqCst);
            let copy_started = Instant::now();
            let snapshot_data = {
                state
                    .inner
                    .store
                    .snapshot_data()
            };
            state
                .inner
                .last_snapshot_copy_micros
                .store(copy_started.elapsed().as_micros() as u64, Ordering::SeqCst);
            let compute_started = Instant::now();
            let snapshot = tokio::task::spawn_blocking(move || snapshot_data.into_cached_snapshot())
                .await
                .expect("snapshot compute task should join");
            state
                .inner
                .last_snapshot_compute_micros
                .store(compute_started.elapsed().as_micros() as u64, Ordering::SeqCst);
            {
                let mut snapshot_guard = state
                    .inner
                    .snapshot
                    .write()
                    .expect("snapshot lock poisoned");
                *snapshot_guard = snapshot.clone();
            }
            let _ = state.inner.dashboard_tx.send(snapshot);
            state
                .inner
                .refresh_in_flight
                .store(false, Ordering::SeqCst);
            state.try_spawn_refresh();
        });
    }

    fn try_spawn_refresh(&self) {
        if !self.inner.dirty.load(Ordering::SeqCst) || !self.inner.demand_seen.load(Ordering::SeqCst)
        {
            return;
        }
        if self
            .inner
            .refresh_in_flight
            .compare_exchange(false, true, Ordering::SeqCst, Ordering::SeqCst)
            .is_ok()
        {
            self.spawn_refresh();
        }
    }
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
    while let Ok(snapshot) = receiver.recv().await {
        if socket
            .send(Message::Text(
                serde_json::to_string(&snapshot)
                    .expect("dashboard snapshot json")
                    .into(),
            ))
            .await
            .is_err()
        {
            return;
        }
    }
}

#[cfg(test)]
mod tests {
    use std::{
        sync::{Arc, atomic::Ordering},
        time::{Duration, Instant},
    };

    use super::AppState;
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

    #[test]
    fn unrelated_sim_writes_do_not_block_on_global_store_contention() {
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
}
