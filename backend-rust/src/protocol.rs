use serde::{Deserialize, Deserializer, Serialize, Serializer, de::Error as DeError};
use serde_json::Value;

#[derive(Clone, Debug, PartialEq)]
pub struct EventEnvelope {
    pub event_type: String,
    pub sim_id: String,
    pub seq: u64,
    pub timestamp_ms: u64,
    pub payload: EventPayload,
}

#[derive(Clone, Debug, PartialEq)]
pub enum EventPayload {
    SimHello(HelloPayload),
    SimFoodSnapshot(FoodSnapshotPayload),
    SimHeartbeat(HeartbeatPayload),
    FoodPickup(FoodPickupPayload),
    FoodDrop(FoodDropPayload),
    AntTurnMove(TurnMovePayload),
    SimGoodbye(GoodbyePayload),
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct HelloPayload {
    pub sim_name: String,
    #[serde(default)]
    pub source: String,
    #[serde(default)]
    pub session_started_ms: u64,
    #[serde(default)]
    pub world_width: f32,
    #[serde(default)]
    pub world_height: f32,
    #[serde(default)]
    pub ant_count: usize,
    #[serde(default)]
    pub food_count: usize,
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct FoodSnapshotPayload {
    pub foods: Vec<StartupFoodPayload>,
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct StartupFoodPayload {
    pub food_id: usize,
    pub x: f32,
    pub y: f32,
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct HeartbeatPayload {
    pub connected_ant_count: usize,
    pub known_food_count: usize,
    pub dropped_outbound_events: usize,
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct FoodPickupPayload {
    #[serde(default)]
    pub ant_id: Option<String>,
    pub food_id: usize,
    #[serde(default)]
    pub x: Option<f32>,
    #[serde(default)]
    pub y: Option<f32>,
    #[serde(default)]
    pub direction_x: Option<f32>,
    #[serde(default)]
    pub direction_y: Option<f32>,
    #[serde(default)]
    pub frame: Option<u64>,
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct FoodDropPayload {
    #[serde(default)]
    pub ant_id: Option<String>,
    pub food_id: usize,
    pub x: f32,
    pub y: f32,
    #[serde(default)]
    pub direction_x: Option<f32>,
    #[serde(default)]
    pub direction_y: Option<f32>,
    #[serde(default)]
    pub frame: Option<u64>,
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct TurnMovePayload {
    pub ant_id: String,
    pub x: f32,
    pub y: f32,
    pub direction_x: f32,
    pub direction_y: f32,
    pub frame: u64,
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct GoodbyePayload {
    pub reason: String,
}

#[derive(Deserialize)]
struct RawEnvelope {
    #[serde(rename = "type")]
    event_type: String,
    sim_id: String,
    seq: u64,
    timestamp_ms: u64,
    payload: Value,
}

#[derive(Serialize)]
struct RawEnvelopeRef<'a> {
    #[serde(rename = "type")]
    event_type: &'a str,
    sim_id: &'a str,
    seq: u64,
    timestamp_ms: u64,
    payload: &'a Value,
}

impl<'de> Deserialize<'de> for EventEnvelope {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        let raw = RawEnvelope::deserialize(deserializer)?;
        let payload = match raw.event_type.as_str() {
            "sim_hello" => EventPayload::SimHello(
                serde_json::from_value(raw.payload).map_err(D::Error::custom)?,
            ),
            "sim_food_snapshot" => EventPayload::SimFoodSnapshot(
                serde_json::from_value(raw.payload).map_err(D::Error::custom)?,
            ),
            "sim_heartbeat" => EventPayload::SimHeartbeat(
                serde_json::from_value(raw.payload).map_err(D::Error::custom)?,
            ),
            "food_pickup" => EventPayload::FoodPickup(
                serde_json::from_value(raw.payload).map_err(D::Error::custom)?,
            ),
            "food_drop" => EventPayload::FoodDrop(
                serde_json::from_value(raw.payload).map_err(D::Error::custom)?,
            ),
            "ant_turn_move" => EventPayload::AntTurnMove(
                serde_json::from_value(raw.payload).map_err(D::Error::custom)?,
            ),
            "sim_goodbye" => EventPayload::SimGoodbye(
                serde_json::from_value(raw.payload).map_err(D::Error::custom)?,
            ),
            other => return Err(D::Error::custom(format!("unknown event type: {other}"))),
        };

        Ok(Self {
            event_type: raw.event_type,
            sim_id: raw.sim_id,
            seq: raw.seq,
            timestamp_ms: raw.timestamp_ms,
            payload,
        })
    }
}

impl Serialize for EventEnvelope {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        let payload = serde_json::to_value(&self.payload).map_err(serde::ser::Error::custom)?;
        RawEnvelopeRef {
            event_type: &self.event_type,
            sim_id: &self.sim_id,
            seq: self.seq,
            timestamp_ms: self.timestamp_ms,
            payload: &payload,
        }
        .serialize(serializer)
    }
}

impl Serialize for EventPayload {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        match self {
            EventPayload::SimHello(payload) => payload.serialize(serializer),
            EventPayload::SimFoodSnapshot(payload) => payload.serialize(serializer),
            EventPayload::SimHeartbeat(payload) => payload.serialize(serializer),
            EventPayload::FoodPickup(payload) => payload.serialize(serializer),
            EventPayload::FoodDrop(payload) => payload.serialize(serializer),
            EventPayload::AntTurnMove(payload) => payload.serialize(serializer),
            EventPayload::SimGoodbye(payload) => payload.serialize(serializer),
        }
    }
}
