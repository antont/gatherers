use std::sync::{
    Arc, RwLock,
    atomic::{AtomicBool, Ordering},
};

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
    store: RwLock<Store>,
    snapshot: RwLock<CachedSnapshot>,
    refresh: Mutex<()>,
    dirty: AtomicBool,
    demand_seen: AtomicBool,
    refresh_in_flight: AtomicBool,
    dashboard_tx: broadcast::Sender<CachedSnapshot>,
}

impl AppState {
    pub fn new() -> Self {
        let (dashboard_tx, _) = broadcast::channel(32);
        Self {
            inner: Arc::new(AppStateInner {
                store: RwLock::new(Store::default()),
                snapshot: RwLock::new(CachedSnapshot::default()),
                refresh: Mutex::new(()),
                dirty: AtomicBool::new(false),
                demand_seen: AtomicBool::new(false),
                refresh_in_flight: AtomicBool::new(false),
                dashboard_tx,
            }),
        }
    }

    pub fn apply_event(&self, envelope: EventEnvelope) -> Result<(), String> {
        self.inner
            .store
            .write()
            .expect("store lock poisoned")
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
            let snapshot_data = {
                state
                    .inner
                    .store
                    .read()
                    .expect("store lock poisoned")
                    .snapshot_data()
            };
            let snapshot = snapshot_data.into_cached_snapshot();
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
        sync::TryLockError,
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
                    match state.inner.store.try_write() {
                        Ok(guard) => {
                            drop(guard);
                            if let Some(started_at) = blocked_since {
                                return started_at.elapsed();
                            }
                        }
                        Err(TryLockError::WouldBlock) => {
                            blocked_since.get_or_insert_with(Instant::now);
                        }
                        Err(TryLockError::Poisoned(_)) => panic!("store lock should not poison"),
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
}
