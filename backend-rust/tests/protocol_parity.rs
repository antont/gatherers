use gatherers_backend_rust::protocol::{
    EventEnvelope, EventPayload, FoodDropPayload, FoodPickupPayload, FoodSnapshotPayload,
    HelloPayload, StartupFoodPayload, TurnMovePayload,
};

#[test]
fn deserializes_sim_hello_from_current_client_shape() {
    let envelope: EventEnvelope = serde_json::from_str(
        r#"{
            "type": "sim_hello",
            "sim_id": "sim-123",
            "seq": 1,
            "timestamp_ms": 0,
            "payload": {
                "sim_name": "sim-123",
                "source": "rust-bevy",
                "session_started_ms": 0,
                "world_width": 1280.0,
                "world_height": 720.0,
                "ant_count": 26,
                "food_count": 80
            }
        }"#,
    )
    .expect("sim_hello should deserialize");

    assert_eq!(envelope.event_type, "sim_hello");
    assert_eq!(envelope.sim_id, "sim-123");
    match envelope.payload {
        EventPayload::SimHello(HelloPayload {
            sim_name,
            source,
            ant_count,
            food_count,
            ..
        }) => {
            assert_eq!(sim_name, "sim-123");
            assert_eq!(source, "rust-bevy");
            assert_eq!(ant_count, 26);
            assert_eq!(food_count, 80);
        }
        other => panic!("expected sim_hello payload, got {other:?}"),
    }
}

#[test]
fn deserializes_sim_food_snapshot_from_current_client_shape() {
    let envelope: EventEnvelope = serde_json::from_str(
        r#"{
            "type": "sim_food_snapshot",
            "sim_id": "sim-123",
            "seq": 2,
            "timestamp_ms": 0,
            "payload": {
                "foods": [
                    { "food_id": "food-1", "x": 10.5, "y": 20.5 },
                    { "food_id": "food-2", "x": 30.5, "y": 40.5 }
                ]
            }
        }"#,
    )
    .expect("sim_food_snapshot should deserialize");

    match envelope.payload {
        EventPayload::SimFoodSnapshot(FoodSnapshotPayload { foods }) => {
            assert_eq!(
                foods,
                vec![
                    StartupFoodPayload {
                        food_id: "food-1".into(),
                        x: 10.5,
                        y: 20.5,
                    },
                    StartupFoodPayload {
                        food_id: "food-2".into(),
                        x: 30.5,
                        y: 40.5,
                    },
                ]
            );
        }
        other => panic!("expected sim_food_snapshot payload, got {other:?}"),
    }
}

#[test]
fn deserializes_food_pickup_and_drop_from_current_client_shape() {
    let pickup: EventEnvelope = serde_json::from_str(
        r#"{
            "type": "food_pickup",
            "sim_id": "sim-123",
            "seq": 3,
            "timestamp_ms": 0,
            "payload": {
                "ant_id": "ant-1",
                "food_id": "food-1",
                "x": 1.0,
                "y": 2.0,
                "direction_x": -0.5,
                "direction_y": 0.5,
                "frame": 7
            }
        }"#,
    )
    .expect("food_pickup should deserialize");

    match pickup.payload {
        EventPayload::FoodPickup(FoodPickupPayload {
            ant_id, food_id, ..
        }) => {
            assert_eq!(ant_id.as_deref(), Some("ant-1"));
            assert_eq!(food_id, "food-1");
        }
        other => panic!("expected food_pickup payload, got {other:?}"),
    }

    let drop_event: EventEnvelope = serde_json::from_str(
        r#"{
            "type": "food_drop",
            "sim_id": "sim-123",
            "seq": 4,
            "timestamp_ms": 0,
            "payload": {
                "ant_id": "ant-1",
                "food_id": "food-1",
                "x": 3.0,
                "y": 4.0,
                "direction_x": 0.25,
                "direction_y": -0.25,
                "frame": 8
            }
        }"#,
    )
    .expect("food_drop should deserialize");

    match drop_event.payload {
        EventPayload::FoodDrop(FoodDropPayload {
            ant_id, food_id, ..
        }) => {
            assert_eq!(ant_id.as_deref(), Some("ant-1"));
            assert_eq!(food_id, "food-1");
        }
        other => panic!("expected food_drop payload, got {other:?}"),
    }
}

#[test]
fn deserializes_ant_turn_move_from_current_client_shape() {
    let envelope: EventEnvelope = serde_json::from_str(
        r#"{
            "type": "ant_turn_move",
            "sim_id": "sim-123",
            "seq": 5,
            "timestamp_ms": 0,
            "payload": {
                "ant_id": "ant-1",
                "x": 3.0,
                "y": 4.0,
                "direction_x": 0.25,
                "direction_y": -0.25,
                "frame": 8
            }
        }"#,
    )
    .expect("ant_turn_move should deserialize");

    match envelope.payload {
        EventPayload::AntTurnMove(TurnMovePayload { ant_id, frame, .. }) => {
            assert_eq!(ant_id, "ant-1");
            assert_eq!(frame, 8);
        }
        other => panic!("expected ant_turn_move payload, got {other:?}"),
    }
}
