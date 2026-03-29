use axum::{
    body::{Body, to_bytes},
    http::{Request, StatusCode},
};
use gatherers_backend_rust::{
    app::{AppState, build_router, build_router_with_state},
    protocol::{
        EventEnvelope, EventPayload, FoodDropPayload, FoodPickupPayload, FoodSnapshotPayload,
        HelloPayload, StartupFoodPayload, TurnMovePayload,
    },
};
use serde_json::Value;
use tower::ServiceExt;

#[tokio::test]
async fn serves_empty_state_http_surface_with_dashboard_bootstrap_ids() {
    let app = build_router();

    let health = app
        .clone()
        .oneshot(
            Request::builder()
                .uri("/healthz")
                .body(Body::empty())
                .expect("healthz request"),
        )
        .await
        .expect("healthz response");
    assert_eq!(health.status(), StatusCode::OK);

    let summary = app
        .clone()
        .oneshot(
            Request::builder()
                .uri("/api/summary")
                .body(Body::empty())
                .expect("summary request"),
        )
        .await
        .expect("summary response");
    assert_eq!(summary.status(), StatusCode::OK);
    let summary_body = to_bytes(summary.into_body(), usize::MAX)
        .await
        .expect("summary body");
    let summary_json: Value = serde_json::from_slice(&summary_body).expect("summary json");
    assert_eq!(summary_json["live_summary"]["connected_sim_count"], 0);
    assert_eq!(summary_json["live_summary"]["loose_food_count"], 0);
    assert_eq!(summary_json["analytics_summary"]["occupied_cell_count"], 0);
    assert_eq!(
        summary_json["analytics_meta"]["is_stale"],
        true,
        "expected analytics metadata to mark the empty bootstrap snapshot as stale"
    );

    let sims = app
        .clone()
        .oneshot(
            Request::builder()
                .uri("/api/sims")
                .body(Body::empty())
                .expect("sims request"),
        )
        .await
        .expect("sims response");
    assert_eq!(sims.status(), StatusCode::OK);
    let sims_body = to_bytes(sims.into_body(), usize::MAX)
        .await
        .expect("sims body");
    assert_eq!(
        std::str::from_utf8(&sims_body).expect("sims utf8"),
        "[]",
    );

    let dashboard = app
        .oneshot(
            Request::builder()
                .uri("/")
                .body(Body::empty())
                .expect("dashboard request"),
        )
        .await
        .expect("dashboard response");
    assert_eq!(dashboard.status(), StatusCode::OK);

    let dashboard_body = String::from_utf8(
        to_bytes(dashboard.into_body(), usize::MAX)
            .await
            .expect("dashboard body")
            .to_vec(),
    )
    .expect("dashboard utf8");
    for required in [
        "Connected sims",
        "connected-sims-value",
        "loose-food-value",
        "elapsed-seconds-value",
        "events-per-second-value",
        "occupied-cells-value",
        "nearest-neighbor-value",
        "analytics-age-value",
        "sim-table-body",
        "/ws/dashboard",
    ] {
        assert!(
            dashboard_body.contains(required),
            "expected dashboard HTML to contain {required:?}, body was {dashboard_body:?}"
        );
    }
}

#[tokio::test]
async fn api_summary_exposes_live_counts_immediately_and_analytics_catch_up_after_direct_ingest() {
    let state = AppState::new();
    let app = build_router_with_state(state.clone());

    state.apply_event(EventEnvelope {
        event_type: "sim_hello".into(),
        sim_id: "sim-a".into(),
        seq: 1,
        timestamp_ms: 0,
        payload: EventPayload::SimHello(HelloPayload {
            sim_name: "sim-a".into(),
            source: "rust-bevy".into(),
            session_started_ms: 0,
            world_width: 1280.0,
            world_height: 720.0,
            ant_count: 26,
            food_count: 3,
        }),
    })
    .expect("sim_hello should be accepted");
    state.apply_event(EventEnvelope {
        event_type: "sim_food_snapshot".into(),
        sim_id: "sim-a".into(),
        seq: 2,
        timestamp_ms: 0,
        payload: EventPayload::SimFoodSnapshot(FoodSnapshotPayload {
            foods: vec![
                StartupFoodPayload {
                    food_id: 0,
                    x: 10.0,
                    y: 20.0,
                },
                StartupFoodPayload {
                    food_id: 1,
                    x: 30.0,
                    y: 40.0,
                },
                StartupFoodPayload {
                    food_id: 2,
                    x: 50.0,
                    y: 60.0,
                },
            ],
        }),
    })
    .expect("sim_food_snapshot should be accepted");

    let initial = fetch_json(&app, "/api/summary").await;
    assert_eq!(initial["live_summary"]["connected_sim_count"], 1);
    assert_eq!(initial["live_summary"]["loose_food_count"], 3);
    assert_eq!(initial["analytics_summary"]["occupied_cell_count"], 0);
    assert_eq!(initial["analytics_meta"]["is_stale"], true);

    let eventually = wait_for_json(&app, "/api/summary", |json| {
        json["live_summary"]["connected_sim_count"] == 1
            && json["live_summary"]["loose_food_count"] == 3
            && json["live_summary"]["elapsed_seconds"]
                .as_f64()
                .unwrap_or_default()
                > 0.0
            && json["live_summary"]["events_per_second"]
                .as_f64()
                .unwrap_or_default()
                > 0.0
            && json["analytics_meta"]["computed_at_ms"]
                .as_i64()
                .unwrap_or_default()
                > 0
            && json["analytics_meta"]["is_stale"] == false
    })
    .await;

    assert_eq!(eventually["live_summary"]["connected_sim_count"], 1);
    assert_eq!(eventually["live_summary"]["loose_food_count"], 3);
    assert_eq!(eventually["analytics_summary"]["occupied_cell_count"], 2);
}

#[tokio::test]
async fn api_summary_keeps_live_counts_fresh_even_when_analytics_are_stale() {
    let state = AppState::new();
    let app = build_router_with_state(state.clone());

    let sid = "sim-live-vs-analytics";
    state.apply_event(EventEnvelope {
        event_type: "sim_hello".into(),
        sim_id: sid.into(),
        seq: 1,
        timestamp_ms: 0,
        payload: EventPayload::SimHello(HelloPayload {
            sim_name: sid.into(),
            source: "rust-bevy".into(),
            session_started_ms: 0,
            world_width: 1280.0,
            world_height: 720.0,
            ant_count: 26,
            food_count: 2,
        }),
    })
    .expect("sim_hello should be accepted");
    state.apply_event(EventEnvelope {
        event_type: "sim_food_snapshot".into(),
        sim_id: sid.into(),
        seq: 2,
        timestamp_ms: 0,
        payload: EventPayload::SimFoodSnapshot(FoodSnapshotPayload {
            foods: vec![
                StartupFoodPayload { food_id: 0, x: 12.0, y: 18.0 },
                StartupFoodPayload { food_id: 1, x: 22.0, y: 28.0 },
            ],
        }),
    })
    .expect("sim_food_snapshot should be accepted");
    state.apply_event(EventEnvelope {
        event_type: "food_pickup".into(),
        sim_id: sid.into(),
        seq: 3,
        timestamp_ms: 0,
        payload: EventPayload::FoodPickup(FoodPickupPayload {
            ant_id: Some("ant-1".into()),
            food_id: 0,
            x: None, y: None, direction_x: None, direction_y: None, frame: None,
        }),
    })
    .expect("pickup 0");
    state.apply_event(EventEnvelope {
        event_type: "food_pickup".into(),
        sim_id: sid.into(),
        seq: 4,
        timestamp_ms: 0,
        payload: EventPayload::FoodPickup(FoodPickupPayload {
            ant_id: Some("ant-1".into()),
            food_id: 1,
            x: None, y: None, direction_x: None, direction_y: None, frame: None,
        }),
    })
    .expect("pickup 1");
    state.apply_event(EventEnvelope {
        event_type: "food_drop".into(),
        sim_id: sid.into(),
        seq: 5,
        timestamp_ms: 0,
        payload: EventPayload::FoodDrop(FoodDropPayload {
            ant_id: Some("ant-1".into()),
            food_id: 0,
            x: 12.0,
            y: 18.0,
            direction_x: Some(0.0),
            direction_y: Some(1.0),
            frame: Some(5),
        }),
    })
    .expect("first food_drop should be accepted");

    let warmed = wait_for_json(&app, "/api/summary", |json| {
        json["live_summary"]["loose_food_count"] == 1
            && json["analytics_summary"]["occupied_cell_count"] == 1
            && json["analytics_meta"]["is_stale"] == false
    })
    .await;
    let previous_analytics_timestamp = warmed["analytics_meta"]["computed_at_ms"]
        .as_i64()
        .expect("analytics timestamp");

    state.apply_event(EventEnvelope {
        event_type: "food_drop".into(),
        sim_id: sid.into(),
        seq: 6,
        timestamp_ms: 0,
        payload: EventPayload::FoodDrop(FoodDropPayload {
            ant_id: Some("ant-1".into()),
            food_id: 1,
            x: 22.0,
            y: 28.0,
            direction_x: Some(0.0),
            direction_y: Some(1.0),
            frame: Some(6),
        }),
    })
    .expect("second food_drop should be accepted");

    let split = wait_for_json(&app, "/api/summary", |json| {
        json["live_summary"]["loose_food_count"] == 2
            && json["analytics_summary"]["occupied_cell_count"] == 1
            && json["analytics_meta"]["is_stale"] == true
    })
    .await;

    assert_eq!(split["live_summary"]["loose_food_count"], 2);
    assert_eq!(split["analytics_summary"]["occupied_cell_count"], 1);
    assert_eq!(split["analytics_meta"]["is_stale"], true);
    assert_eq!(
        split["analytics_meta"]["computed_at_ms"]
            .as_i64()
            .expect("analytics timestamp"),
        previous_analytics_timestamp
    );
}

#[tokio::test]
async fn api_sims_catches_up_to_direct_ingest_counts() {
    let state = AppState::new();
    let app = build_router_with_state(state.clone());

    state.apply_event(EventEnvelope {
        event_type: "sim_hello".into(),
        sim_id: "sim-b".into(),
        seq: 1,
        timestamp_ms: 0,
        payload: EventPayload::SimHello(HelloPayload {
            sim_name: "sim-b".into(),
            source: "rust-bevy".into(),
            session_started_ms: 0,
            world_width: 1280.0,
            world_height: 720.0,
            ant_count: 26,
            food_count: 3,
        }),
    })
    .expect("sim_hello should be accepted");
    state.apply_event(EventEnvelope {
        event_type: "sim_food_snapshot".into(),
        sim_id: "sim-b".into(),
        seq: 2,
        timestamp_ms: 0,
        payload: EventPayload::SimFoodSnapshot(FoodSnapshotPayload {
            foods: vec![
                StartupFoodPayload { food_id: 0, x: 10.0, y: 20.0 },
                StartupFoodPayload { food_id: 1, x: 30.0, y: 40.0 },
                StartupFoodPayload { food_id: 2, x: 50.0, y: 60.0 },
            ],
        }),
    })
    .expect("sim_food_snapshot should be accepted");
    state.apply_event(EventEnvelope {
        event_type: "food_pickup".into(),
        sim_id: "sim-b".into(),
        seq: 3,
        timestamp_ms: 0,
        payload: EventPayload::FoodPickup(FoodPickupPayload {
            ant_id: Some("ant-1".into()),
            food_id: 0,
            x: Some(10.0),
            y: Some(20.0),
            direction_x: Some(1.0),
            direction_y: Some(0.0),
            frame: Some(1),
        }),
    })
    .expect("food_pickup should be accepted");
    state.apply_event(EventEnvelope {
        event_type: "food_drop".into(),
        sim_id: "sim-b".into(),
        seq: 4,
        timestamp_ms: 0,
        payload: EventPayload::FoodDrop(FoodDropPayload {
            ant_id: Some("ant-1".into()),
            food_id: 0,
            x: 70.0,
            y: 80.0,
            direction_x: Some(0.0),
            direction_y: Some(1.0),
            frame: Some(2),
        }),
    })
    .expect("food_drop should be accepted");
    state.apply_event(EventEnvelope {
        event_type: "ant_turn_move".into(),
        sim_id: "sim-b".into(),
        seq: 5,
        timestamp_ms: 0,
        payload: EventPayload::AntTurnMove(TurnMovePayload {
            ant_id: "ant-1".into(),
            x: 70.0,
            y: 80.0,
            direction_x: 0.0,
            direction_y: 1.0,
            frame: 3,
        }),
    })
    .expect("ant_turn_move should be accepted");

    let initial = fetch_json(&app, "/api/sims").await;
    let initial_sims = initial.as_array().expect("initial sims array");
    assert_eq!(initial_sims.len(), 1);
    assert_eq!(initial_sims[0]["sim_id"], "sim-b");

    let eventually = wait_for_json(&app, "/api/sims", |json| {
        json.as_array()
            .and_then(|items| items.first())
            .map(|sim| {
                sim["sim_id"] == "sim-b"
                    && sim["ant_count"] == 26
                    && sim["pickup_count"] == 1
                    && sim["drop_count"] == 1
                    && sim["turn_move_count"] == 1
                    && sim["loose_food_count"] == 3
            })
            .unwrap_or(false)
    })
    .await;

    let sims = eventually.as_array().expect("sims array");
    assert_eq!(sims.len(), 1);
}

#[tokio::test]
async fn api_sims_is_capped_to_twenty_entries() {
    let state = AppState::new();
    let app = build_router_with_state(state.clone());

    for index in 0..25 {
        state.apply_event(EventEnvelope {
            event_type: "sim_hello".into(),
            sim_id: format!("sim-{index:02}"),
            seq: 1,
            timestamp_ms: 0,
            payload: EventPayload::SimHello(HelloPayload {
                sim_name: format!("sim-{index:02}"),
                source: "rust-bevy".into(),
                session_started_ms: 0,
                world_width: 1280.0,
                world_height: 720.0,
                ant_count: 26,
                food_count: 0,
            }),
        })
        .expect("sim_hello should be accepted");
    }

    let sims = fetch_json(&app, "/api/sims").await;
    let sims = sims.as_array().expect("sims array");
    assert_eq!(sims.len(), 20, "api/sims should only return the first 20 sims");
}

#[tokio::test]
async fn breakpoint_totals_is_empty_for_unknown_prefix() {
    let app = build_router();

    let totals = fetch_json(&app, "/api/breakpoint_totals?prefix=missing").await;
    assert_eq!(totals["connected_sims"], 0);
    assert_eq!(totals["pickup_count"], 0);
    assert_eq!(totals["drop_count"], 0);
    assert_eq!(totals["turn_move_count"], 0);
    assert_eq!(totals["loose_food_count"], 0);
}

#[tokio::test]
async fn breakpoint_totals_aggregates_only_matching_prefix() {
    let state = AppState::new();
    let app = build_router_with_state(state.clone());

    for sim_id in ["bp-run-1-a", "bp-run-1-b", "other-run-1"] {
        state.apply_event(EventEnvelope {
            event_type: "sim_hello".into(),
            sim_id: sim_id.into(),
            seq: 1,
            timestamp_ms: 0,
            payload: EventPayload::SimHello(HelloPayload {
                sim_name: sim_id.into(),
                source: "rust-bevy".into(),
                session_started_ms: 0,
                world_width: 1280.0,
                world_height: 720.0,
                ant_count: 26,
                food_count: 3,
            }),
        })
        .expect("sim_hello should be accepted");
    }

    for (sim_id, base_x) in [("bp-run-1-a", 10.0), ("bp-run-1-b", 20.0), ("other-run-1", 30.0)] {
        state.apply_event(EventEnvelope {
            event_type: "sim_food_snapshot".into(),
            sim_id: sim_id.into(),
            seq: 2,
            timestamp_ms: 0,
            payload: EventPayload::SimFoodSnapshot(FoodSnapshotPayload {
                foods: vec![
                    StartupFoodPayload { food_id: 0, x: base_x, y: base_x },
                    StartupFoodPayload { food_id: 1, x: base_x + 1.0, y: base_x + 1.0 },
                    StartupFoodPayload { food_id: 2, x: base_x + 2.0, y: base_x + 2.0 },
                ],
            }),
        })
        .expect("sim_food_snapshot should be accepted");
    }

    for (seq, sim_id) in [(3, "bp-run-1-a"), (4, "bp-run-1-b"), (5, "other-run-1")] {
        state.apply_event(EventEnvelope {
            event_type: "food_pickup".into(),
            sim_id: sim_id.into(),
            seq,
            timestamp_ms: 0,
            payload: EventPayload::FoodPickup(FoodPickupPayload {
                ant_id: Some(format!("{sim_id}-ant")),
                food_id: 0,
                x: None,
                y: None,
                direction_x: None,
                direction_y: None,
                frame: None,
            }),
        })
        .expect("food_pickup should be accepted");
        state.apply_event(EventEnvelope {
            event_type: "food_drop".into(),
            sim_id: sim_id.into(),
            seq: seq + 10,
            timestamp_ms: 0,
            payload: EventPayload::FoodDrop(FoodDropPayload {
                ant_id: Some(format!("{sim_id}-ant")),
                food_id: 0,
                x: 99.0,
                y: 101.0,
                direction_x: Some(0.0),
                direction_y: Some(1.0),
                frame: Some(seq + 10),
            }),
        })
        .expect("food_drop should be accepted");
        state.apply_event(EventEnvelope {
            event_type: "ant_turn_move".into(),
            sim_id: sim_id.into(),
            seq: seq + 20,
            timestamp_ms: 0,
            payload: EventPayload::AntTurnMove(TurnMovePayload {
                ant_id: format!("{sim_id}-ant"),
                x: 99.0,
                y: 101.0,
                direction_x: 0.0,
                direction_y: 1.0,
                frame: seq + 20,
            }),
        })
        .expect("ant_turn_move should be accepted");
    }

    let totals = fetch_json(&app, "/api/breakpoint_totals?prefix=bp-run-1").await;
    assert_eq!(totals["connected_sims"], 2);
    assert_eq!(totals["pickup_count"], 2);
    assert_eq!(totals["drop_count"], 2);
    assert_eq!(totals["turn_move_count"], 2);
    assert_eq!(totals["loose_food_count"], 6);
}

#[tokio::test]
async fn breakpoint_totals_is_not_affected_by_sims_api_cap() {
    let state = AppState::new();
    let app = build_router_with_state(state.clone());

    for index in 0..25 {
        let sim_id = format!("bp-cap-{index:02}");
        state.apply_event(EventEnvelope {
            event_type: "sim_hello".into(),
            sim_id: sim_id.clone(),
            seq: 1,
            timestamp_ms: 0,
            payload: EventPayload::SimHello(HelloPayload {
                sim_name: sim_id,
                source: "rust-bevy".into(),
                session_started_ms: 0,
                world_width: 1280.0,
                world_height: 720.0,
                ant_count: 26,
                food_count: 0,
            }),
        })
        .expect("sim_hello should be accepted");
    }

    let sims = fetch_json(&app, "/api/sims").await;
    assert_eq!(sims.as_array().expect("sims array").len(), 20);

    let totals = fetch_json(&app, "/api/breakpoint_totals?prefix=bp-cap-").await;
    assert_eq!(totals["connected_sims"], 25);
    assert_eq!(totals["pickup_count"], 0);
    assert_eq!(totals["drop_count"], 0);
    assert_eq!(totals["turn_move_count"], 0);
    assert_eq!(totals["loose_food_count"], 0);
}

async fn fetch_json<S>(app: &S, uri: &str) -> Value
where
    S: tower::Service<Request<Body>, Response = axum::response::Response> + Clone,
    S::Future: Send,
    S::Error: std::fmt::Debug,
{
    let response = app
        .clone()
        .oneshot(
            Request::builder()
                .uri(uri)
                .body(Body::empty())
                .expect("request"),
        )
        .await
        .expect("response");
    let body = to_bytes(response.into_body(), usize::MAX)
        .await
        .expect("body");
    serde_json::from_slice(&body).expect("json body")
}

async fn wait_for_json<S, F>(app: &S, uri: &str, match_json: F) -> Value
where
    S: tower::Service<Request<Body>, Response = axum::response::Response> + Clone,
    S::Future: Send,
    S::Error: std::fmt::Debug,
    F: Fn(&Value) -> bool,
{
    let deadline = tokio::time::Instant::now() + std::time::Duration::from_millis(1500);
    loop {
        let json = fetch_json(app, uri).await;
        if match_json(&json) {
            return json;
        }
        assert!(
            tokio::time::Instant::now() < deadline,
            "expected {uri} to converge before timeout, last payload was {json:?}"
        );
        tokio::time::sleep(std::time::Duration::from_millis(25)).await;
    }
}
