use std::sync::{Arc, Mutex, RwLock};

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
use tokio::sync::broadcast;

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
    refresh: Mutex<RefreshState>,
    dashboard_tx: broadcast::Sender<CachedSnapshot>,
}

#[derive(Default)]
struct RefreshState {
    dirty: bool,
    demand_seen: bool,
    refresh_in_flight: bool,
}

impl AppState {
    pub fn new() -> Self {
        let (dashboard_tx, _) = broadcast::channel(32);
        Self {
            inner: Arc::new(AppStateInner {
                store: RwLock::new(Store::default()),
                snapshot: RwLock::new(CachedSnapshot::default()),
                refresh: Mutex::new(RefreshState::default()),
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

        let should_spawn = {
            let mut refresh = self.inner.refresh.lock().expect("refresh lock poisoned");
            refresh.dirty = true;
            if refresh.demand_seen && !refresh.refresh_in_flight {
                refresh.refresh_in_flight = true;
                true
            } else {
                false
            }
        };
        if should_spawn {
            self.spawn_refresh();
        }
        Ok(())
    }

    fn current_snapshot(&self) -> CachedSnapshot {
        self.inner
            .snapshot
            .read()
            .expect("snapshot lock poisoned")
            .clone()
    }

    fn register_api_demand(&self) {
        let should_spawn = {
            let mut refresh = self.inner.refresh.lock().expect("refresh lock poisoned");
            refresh.demand_seen = true;
            if refresh.dirty && !refresh.refresh_in_flight {
                refresh.refresh_in_flight = true;
                true
            } else {
                false
            }
        };

        if should_spawn {
            self.spawn_refresh();
        }
    }

    fn add_dashboard_watcher(&self) -> broadcast::Receiver<CachedSnapshot> {
        let receiver = self.inner.dashboard_tx.subscribe();
        self.register_api_demand();
        receiver
    }

    fn spawn_refresh(&self) {
        let state = self.clone();
        tokio::spawn(async move {
            let snapshot = state
                .inner
                .store
                .read()
                .expect("store lock poisoned")
                .snapshot();
            {
                let mut snapshot_guard = state
                    .inner
                    .snapshot
                    .write()
                    .expect("snapshot lock poisoned");
                *snapshot_guard = snapshot.clone();
            }
            let _ = state.inner.dashboard_tx.send(snapshot);
            let mut refresh = state.inner.refresh.lock().expect("refresh lock poisoned");
            refresh.dirty = false;
            refresh.refresh_in_flight = false;
        });
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
    state.register_api_demand();
    Json(snapshot.summary)
}

async fn sims(State(state): State<AppState>) -> Json<Vec<SimSummaryResponse>> {
    let snapshot = state.current_snapshot();
    state.register_api_demand();
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
        if handle_ingest_event(&state, event).is_err() {
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

    let mut receiver = state.add_dashboard_watcher();
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
