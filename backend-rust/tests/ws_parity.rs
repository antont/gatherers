use std::net::SocketAddr;

use futures_util::{SinkExt, StreamExt};
use gatherers_backend_rust::{
    app::{AppState, build_router_with_state},
    protocol::{
        EventEnvelope, EventPayload, FoodDropPayload, HelloPayload,
    },
};
use serde_json::Value;
use tokio::net::TcpListener;
use tokio_tungstenite::{connect_async, tungstenite::Message};

#[tokio::test]
async fn dashboard_websocket_receives_update_after_ingest_websocket_events() {
    let state = AppState::new();
    let base_url = spawn_test_server(state.clone()).await;

    let (mut dashboard_ws, _) = connect_async(format!("{base_url}/ws/dashboard"))
        .await
        .expect("dashboard websocket should connect");

    let initial = read_json_message(&mut dashboard_ws).await;
    assert_eq!(initial["summary"]["live_summary"]["connected_sim_count"], 0);
    assert_eq!(initial["summary"]["analytics_meta"]["is_stale"], true);

    let (mut ingest_ws, _) = connect_async(format!("{base_url}/ws/ingest"))
        .await
        .expect("ingest websocket should connect");

    ingest_ws
        .send(Message::Text(
            serde_json::to_string(&EventEnvelope {
                event_type: "sim_hello".into(),
                sim_id: "sim-dashboard".into(),
                seq: 1,
                timestamp_ms: 0,
                payload: EventPayload::SimHello(HelloPayload {
                    sim_name: "sim-dashboard".into(),
                    source: "rust-bevy".into(),
                    session_started_ms: 0,
                    world_width: 1280.0,
                    world_height: 720.0,
                    ant_count: 26,
                    food_count: 1,
                }),
            })
            .expect("sim_hello json")
            .into(),
        ))
        .await
        .expect("sim_hello send");
    ingest_ws
        .send(Message::Text(
            serde_json::to_string(&EventEnvelope {
                event_type: "food_drop".into(),
                sim_id: "sim-dashboard".into(),
                seq: 2,
                timestamp_ms: 0,
                payload: EventPayload::FoodDrop(FoodDropPayload {
                    ant_id: Some("ant-1".into()),
                    food_id: "food-1".into(),
                    x: 25.0,
                    y: 25.0,
                    direction_x: Some(0.0),
                    direction_y: Some(1.0),
                    frame: Some(2),
                }),
            })
            .expect("food_drop json")
            .into(),
        ))
        .await
        .expect("food_drop send");

    let update = wait_for_json_message(&mut dashboard_ws, |json| {
        json["summary"]["live_summary"]["connected_sim_count"] == 1
            && json["summary"]["live_summary"]["loose_food_count"] == 1
            && json["summary"]["analytics_summary"]["occupied_cell_count"] == 1
            && json["summary"]["analytics_meta"]["is_stale"] == false
            && json["sims"][0]["sim_id"] == "sim-dashboard"
            && json["sims"][0]["ant_count"] == 26
            && json["sims"][0]["drop_count"] == 1
            && json["sims"][0]["loose_food_count"] == 1
    })
    .await;

    assert_eq!(update["summary"]["live_summary"]["connected_sim_count"], 1);
}

#[tokio::test]
async fn dashboard_websocket_exposes_live_counts_immediately_then_analytics_catch_up_after_direct_ingest() {
    let state = AppState::new();
    state.apply_event(EventEnvelope {
        event_type: "sim_hello".into(),
        sim_id: "sim-lazy-dashboard".into(),
        seq: 1,
        timestamp_ms: 0,
        payload: EventPayload::SimHello(HelloPayload {
            sim_name: "sim-lazy-dashboard".into(),
            source: "rust-bevy".into(),
            session_started_ms: 0,
            world_width: 1280.0,
            world_height: 720.0,
            ant_count: 26,
            food_count: 1,
        }),
    })
    .expect("sim_hello should be accepted");
    state.apply_event(EventEnvelope {
        event_type: "food_drop".into(),
        sim_id: "sim-lazy-dashboard".into(),
        seq: 2,
        timestamp_ms: 0,
        payload: EventPayload::FoodDrop(FoodDropPayload {
            ant_id: Some("ant-1".into()),
            food_id: "food-1".into(),
            x: 12.0,
            y: 18.0,
            direction_x: Some(0.0),
            direction_y: Some(1.0),
            frame: Some(2),
        }),
    })
    .expect("food_drop should be accepted");

    let base_url = spawn_test_server(state.clone()).await;
    let (mut dashboard_ws, _) = connect_async(format!("{base_url}/ws/dashboard"))
        .await
        .expect("dashboard websocket should connect");

    let initial = read_json_message(&mut dashboard_ws).await;
    assert_eq!(initial["summary"]["live_summary"]["connected_sim_count"], 1);
    assert_eq!(initial["summary"]["live_summary"]["loose_food_count"], 1);
    assert_eq!(initial["summary"]["analytics_summary"]["occupied_cell_count"], 0);
    assert_eq!(initial["summary"]["analytics_meta"]["is_stale"], true);

    let update = wait_for_json_message(&mut dashboard_ws, |json| {
        json["summary"]["live_summary"]["connected_sim_count"] == 1
            && json["summary"]["live_summary"]["loose_food_count"] == 1
            && json["summary"]["analytics_summary"]["occupied_cell_count"] == 1
            && json["sims"][0]["sim_id"] == "sim-lazy-dashboard"
            && json["sims"][0]["drop_count"] == 1
            && json["sims"][0]["loose_food_count"] == 1
    })
    .await;

    assert_eq!(update["summary"]["live_summary"]["connected_sim_count"], 1);
}

#[tokio::test]
async fn dashboard_websocket_receives_live_only_updates_without_analytics_recompute() {
    let state = AppState::new();
    let base_url = spawn_test_server(state.clone()).await;

    let (mut dashboard_ws, _) = connect_async(format!("{base_url}/ws/dashboard"))
        .await
        .expect("dashboard websocket should connect");

    let initial = read_json_message(&mut dashboard_ws).await;
    assert_eq!(initial["summary"]["live_summary"]["connected_sim_count"], 0);

    state
        .apply_event(EventEnvelope {
            event_type: "sim_hello".into(),
            sim_id: "sim-live-only".into(),
            seq: 1,
            timestamp_ms: 0,
            payload: EventPayload::SimHello(HelloPayload {
                sim_name: "sim-live-only".into(),
                source: "test".into(),
                session_started_ms: 0,
                world_width: 1280.0,
                world_height: 720.0,
                ant_count: 26,
                food_count: 0,
            }),
        })
        .expect("sim_hello should be accepted");

    let update = tokio::time::timeout(
        std::time::Duration::from_millis(500),
        wait_for_json_message(&mut dashboard_ws, |json| {
            json["summary"]["live_summary"]["connected_sim_count"] == 1
                && json["sims"][0]["sim_id"] == "sim-live-only"
        }),
    )
    .await
    .expect("dashboard should receive live-only sim_hello update within 500ms");

    assert_eq!(update["summary"]["live_summary"]["connected_sim_count"], 1);
    assert_eq!(update["summary"]["analytics_meta"]["is_stale"], true);
}

async fn spawn_test_server(state: AppState) -> String {
    let router = build_router_with_state(state);
    let listener = TcpListener::bind("127.0.0.1:0")
        .await
        .expect("test listener");
    let addr: SocketAddr = listener.local_addr().expect("listener addr");

    tokio::spawn(async move {
        axum::serve(listener, router).await.expect("test server should run");
    });

    format!("ws://{}", addr)
}

async fn read_json_message(
    ws: &mut tokio_tungstenite::WebSocketStream<tokio_tungstenite::MaybeTlsStream<tokio::net::TcpStream>>,
) -> Value {
    let message = ws
        .next()
        .await
        .expect("websocket message")
        .expect("websocket result");
    match message {
        Message::Text(text) => serde_json::from_str(&text).expect("json text"),
        other => panic!("expected text websocket message, got {other:?}"),
    }
}

async fn wait_for_json_message<F>(
    ws: &mut tokio_tungstenite::WebSocketStream<tokio_tungstenite::MaybeTlsStream<tokio::net::TcpStream>>,
    match_json: F,
) -> Value
where
    F: Fn(&Value) -> bool,
{
    loop {
        let json = read_json_message(ws).await;
        if match_json(&json) {
            return json;
        }
    }
}
