use std::{net::SocketAddr, sync::Arc, time::Duration};

use gatherers_backend_rust::{
    app::{AppState, build_router_with_state},
    protocol::{
        EventEnvelope, EventPayload, FoodDropPayload, FoodPickupPayload, FoodSnapshotPayload,
        HelloPayload, StartupFoodPayload, TurnMovePayload,
    },
};
use serde_json::Value;
use tokio::{
    net::TcpListener,
    sync::{Barrier, watch},
};

const CLIENT_COUNT: usize = 40;
const ACTIVITY_TRIPLETS: usize = 20;
const INITIAL_FOOD_COUNT: usize = 80;

#[tokio::test(flavor = "multi_thread", worker_threads = 8)]
async fn heavy_ingest_with_polling_keeps_api_sims_responsive_and_exact() {
    let state = AppState::new();
    let server = spawn_test_server(state.clone()).await;
    let start_barrier = Arc::new(Barrier::new(CLIENT_COUNT + 1));
    let (done_tx, done_rx) = watch::channel(false);

    let poller = tokio::spawn(poll_sims_until_done(server.http_base_url.clone(), done_rx));

    let mut clients = Vec::with_capacity(CLIENT_COUNT);
    for index in 0..CLIENT_COUNT {
        clients.push(tokio::spawn(run_ingest_client(
            state.clone(),
            index,
            start_barrier.clone(),
        )));
    }

    start_barrier.wait().await;
    for client in clients {
        client.await.expect("client task should join");
    }

    done_tx
        .send(true)
        .expect("poller shutdown signal should send");
    poller
        .await
        .expect("poller task should join")
        .expect("api poller should stay responsive under load");

    let totals = wait_for_exact_totals(&server.http_base_url).await;
    assert_eq!(totals.connected_sims, CLIENT_COUNT);
    assert_eq!(totals.pickups, CLIENT_COUNT * ACTIVITY_TRIPLETS);
    assert_eq!(totals.drops, CLIENT_COUNT * ACTIVITY_TRIPLETS);
    assert_eq!(totals.turn_moves, CLIENT_COUNT * ACTIVITY_TRIPLETS);
    assert_eq!(totals.loose_food, CLIENT_COUNT * INITIAL_FOOD_COUNT);
}

async fn run_ingest_client(state: AppState, index: usize, start_barrier: Arc<Barrier>) {
    let sim_id = format!("stress-sim-{index:03}");
    start_barrier.wait().await;
    for event in client_events(&sim_id, index) {
        state
            .apply_event(event)
            .expect("direct ingest event should be accepted");
    }
}

fn client_events(sim_id: &str, index: usize) -> Vec<EventEnvelope> {
    let mut events = Vec::with_capacity(2 + ACTIVITY_TRIPLETS * 3);
    events.push(EventEnvelope {
        event_type: "sim_hello".into(),
        sim_id: sim_id.into(),
        seq: 1,
        timestamp_ms: 0,
        payload: EventPayload::SimHello(HelloPayload {
            sim_name: sim_id.into(),
            source: "rust-load-test".into(),
            session_started_ms: 0,
            world_width: 1280.0,
            world_height: 720.0,
            ant_count: 26,
            food_count: INITIAL_FOOD_COUNT,
        }),
    });
    events.push(EventEnvelope {
        event_type: "sim_food_snapshot".into(),
        sim_id: sim_id.into(),
        seq: 2,
        timestamp_ms: 0,
        payload: EventPayload::SimFoodSnapshot(FoodSnapshotPayload {
            foods: (0..INITIAL_FOOD_COUNT)
                .map(|food_index| StartupFoodPayload {
                    food_id: food_index,
                    x: ((index * 17 + food_index * 13) % 2000) as f32,
                    y: ((index * 19 + food_index * 11) % 2000) as f32,
                })
                .collect(),
        }),
    });

    let mut seq = 3;
    for triplet in 0..ACTIVITY_TRIPLETS {
        let ant_id = format!("{sim_id}-ant-{triplet:03}");
        let x = (index * 31 + triplet * 7) as f32;
        let y = (index * 29 + triplet * 5) as f32;

        events.push(EventEnvelope {
            event_type: "food_pickup".into(),
            sim_id: sim_id.into(),
            seq,
            timestamp_ms: 0,
            payload: EventPayload::FoodPickup(FoodPickupPayload {
                ant_id: Some(ant_id.clone()),
                food_id: triplet,
                x: Some(x),
                y: Some(y),
                direction_x: Some(1.0),
                direction_y: Some(0.0),
                frame: Some(seq),
            }),
        });
        seq += 1;

        events.push(EventEnvelope {
            event_type: "food_drop".into(),
            sim_id: sim_id.into(),
            seq,
            timestamp_ms: 0,
            payload: EventPayload::FoodDrop(FoodDropPayload {
                ant_id: Some(ant_id.clone()),
                food_id: triplet,
                x: x + 0.5,
                y: y + 0.5,
                direction_x: Some(0.0),
                direction_y: Some(1.0),
                frame: Some(seq),
            }),
        });
        seq += 1;

        events.push(EventEnvelope {
            event_type: "ant_turn_move".into(),
            sim_id: sim_id.into(),
            seq,
            timestamp_ms: 0,
            payload: EventPayload::AntTurnMove(TurnMovePayload {
                ant_id,
                x: x + 1.0,
                y: y + 1.0,
                direction_x: 0.0,
                direction_y: 1.0,
                frame: seq,
            }),
        });
        seq += 1;
    }

    events
}

async fn poll_sims_until_done(
    http_base_url: String,
    mut done_rx: watch::Receiver<bool>,
) -> Result<(), String> {
    let client = reqwest::Client::builder()
        .timeout(Duration::from_millis(500))
        .build()
        .map_err(|err| format!("http client build failed: {err}"))?;

    loop {
        if *done_rx.borrow() {
            return Ok(());
        }

        let response = client
            .get(format!("{http_base_url}/api/sims"))
            .send()
            .await
            .map_err(|err| format!("api sims polling failed under load: {err}"))?;
        if !response.status().is_success() {
            return Err(format!(
                "api sims returned unexpected status under load: {}",
                response.status()
            ));
        }

        tokio::select! {
            result = done_rx.changed() => {
                if result.is_err() || *done_rx.borrow() {
                    return Ok(());
                }
            }
            _ = tokio::time::sleep(Duration::from_millis(50)) => {}
        }
    }
}

async fn wait_for_exact_totals(http_base_url: &str) -> Totals {
    let client = reqwest::Client::builder()
        .timeout(Duration::from_secs(2))
        .build()
        .expect("http client should build");
    let deadline = tokio::time::Instant::now() + Duration::from_secs(20);

    loop {
        let totals = fetch_totals(&client, http_base_url).await;
        if totals.connected_sims == CLIENT_COUNT
            && totals.pickups == CLIENT_COUNT * ACTIVITY_TRIPLETS
            && totals.drops == CLIENT_COUNT * ACTIVITY_TRIPLETS
            && totals.turn_moves == CLIENT_COUNT * ACTIVITY_TRIPLETS
            && totals.loose_food == CLIENT_COUNT * INITIAL_FOOD_COUNT
        {
            return totals;
        }

        assert!(
            tokio::time::Instant::now() < deadline,
            "expected exact totals before timeout, last totals were {totals:?}"
        );
        tokio::time::sleep(Duration::from_millis(100)).await;
    }
}

async fn fetch_totals(client: &reqwest::Client, http_base_url: &str) -> Totals {
    let sims: Vec<Value> = client
        .get(format!("{http_base_url}/api/sims"))
        .send()
        .await
        .expect("api sims request should succeed")
        .json()
        .await
        .expect("api sims json should decode");

    sims.into_iter().fold(Totals::default(), |mut totals, sim| {
        totals.connected_sims += 1;
        totals.pickups += sim["pickup_count"].as_u64().unwrap_or_default() as usize;
        totals.drops += sim["drop_count"].as_u64().unwrap_or_default() as usize;
        totals.turn_moves += sim["turn_move_count"].as_u64().unwrap_or_default() as usize;
        totals.loose_food += sim["loose_food_count"].as_u64().unwrap_or_default() as usize;
        totals
    })
}

#[derive(Debug, Default, PartialEq, Eq)]
struct Totals {
    connected_sims: usize,
    pickups: usize,
    drops: usize,
    turn_moves: usize,
    loose_food: usize,
}

struct TestServer {
    http_base_url: String,
}

async fn spawn_test_server(state: AppState) -> TestServer {
    let router = build_router_with_state(state);
    let listener = TcpListener::bind("127.0.0.1:0")
        .await
        .expect("test listener");
    let addr: SocketAddr = listener.local_addr().expect("listener addr");

    tokio::spawn(async move {
        axum::serve(listener, router)
            .await
            .expect("test server should run");
    });

    TestServer {
        http_base_url: format!("http://{addr}"),
    }
}
