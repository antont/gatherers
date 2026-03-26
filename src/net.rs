use std::collections::VecDeque;

use bevy::{prelude::*, window::PrimaryWindow};
use ewebsock::{Options, WsEvent, WsMessage, WsReceiver, WsSender};
use log::{error, warn};
use serde::Serialize;

use crate::{Ant, Food};

pub struct BackendClientPlugin;

impl Plugin for BackendClientPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<BackendClientConfig>()
            .init_resource::<PendingBackendEvents>()
            .init_resource::<BackendSequence>()
            .add_message::<BackendSimEvent>()
            .insert_non_send_resource(BackendConnectionState::default())
            .add_systems(
                PostUpdate,
                (
                    queue_backend_hello_event,
                    queue_backend_food_snapshot_event,
                    collect_backend_events,
                    flush_backend_events,
                )
                    .chain(),
            );
    }
}

#[derive(Resource, Clone, Debug)]
pub struct BackendClientConfig {
    pub url: Option<String>,
    pub sim_id: String,
}

impl Default for BackendClientConfig {
    fn default() -> Self {
        let url = std::env::var("GATHERERS_BACKEND_WS_URL").ok();
        Self {
            url,
            sim_id: format!("sim-{}", rand::random::<u64>()),
        }
    }
}

impl BackendClientConfig {
    pub fn enabled(url: String, sim_id: String) -> Self {
        Self {
            url: Some(url),
            sim_id,
        }
    }

    pub fn is_enabled(&self) -> bool {
        self.url.is_some()
    }
}

#[derive(Resource, Default, Debug)]
pub struct PendingBackendEvents {
    queued: VecDeque<String>,
}

impl PendingBackendEvents {
    pub fn queued_json_messages(&self) -> Vec<String> {
        self.queued.iter().cloned().collect()
    }

    fn push(&mut self, message: String) {
        self.queued.push_back(message);
    }

    fn pop(&mut self) -> Option<String> {
        self.queued.pop_front()
    }
}

#[derive(Resource, Default)]
struct BackendSequence(u64);

#[derive(Default)]
struct BackendConnectionState {
    sender: Option<WsSender>,
    receiver: Option<WsReceiver>,
    opened: bool,
    hello_queued: bool,
    food_snapshot_queued: bool,
}

#[derive(Message, Clone, Debug)]
pub enum BackendSimEvent {
    FoodPickup {
        ant_id: String,
        food_id: String,
        x: f32,
        y: f32,
        direction_x: f32,
        direction_y: f32,
        frame: u64,
    },
    FoodDrop {
        ant_id: String,
        food_id: String,
        x: f32,
        y: f32,
        direction_x: f32,
        direction_y: f32,
        frame: u64,
    },
    AntTurnMove {
        ant_id: String,
        x: f32,
        y: f32,
        direction_x: f32,
        direction_y: f32,
        frame: u64,
    },
}

#[derive(Serialize)]
struct EventEnvelope<T: Serialize> {
    #[serde(rename = "type")]
    event_type: &'static str,
    sim_id: String,
    seq: u64,
    timestamp_ms: u64,
    payload: T,
}

#[derive(Serialize)]
struct HelloPayload {
    sim_name: String,
    source: &'static str,
    session_started_ms: u64,
    world_width: f32,
    world_height: f32,
    ant_count: usize,
    food_count: usize,
}

#[derive(Serialize)]
struct FoodSnapshotPayload {
    foods: Vec<StartupFoodPayload>,
}

#[derive(Serialize)]
struct StartupFoodPayload {
    food_id: String,
    x: f32,
    y: f32,
}

#[derive(Serialize)]
struct FoodEventPayload {
    ant_id: String,
    food_id: String,
    x: f32,
    y: f32,
    direction_x: f32,
    direction_y: f32,
    frame: u64,
}

#[derive(Serialize)]
struct TurnMovePayload {
    ant_id: String,
    x: f32,
    y: f32,
    direction_x: f32,
    direction_y: f32,
    frame: u64,
}

fn queue_backend_hello_event(
    config: Res<BackendClientConfig>,
    mut pending: ResMut<PendingBackendEvents>,
    mut sequence: ResMut<BackendSequence>,
    mut connection: NonSendMut<BackendConnectionState>,
    ant_query: Query<Entity, With<Ant>>,
    food_query: Query<Entity, With<Food>>,
    window_query: Query<&Window, With<PrimaryWindow>>,
) {
    if !config.is_enabled() || connection.hello_queued {
        return;
    }

    let (world_width, world_height) = match window_query.single() {
        Ok(window) => (window.width(), window.height()),
        Err(_) => (0.0, 0.0),
    };

    let envelope = EventEnvelope {
        event_type: "sim_hello",
        sim_id: config.sim_id.clone(),
        seq: next_sequence(&mut sequence),
        timestamp_ms: 0,
        payload: HelloPayload {
            sim_name: config.sim_id.clone(),
            source: "rust-bevy",
            session_started_ms: 0,
            world_width,
            world_height,
            ant_count: ant_query.iter().count(),
            food_count: food_query.iter().count(),
        },
    };

    match serde_json::to_string(&envelope) {
        Ok(json) => {
            pending.push(json);
            connection.hello_queued = true;
        }
        Err(err) => error!("Failed to serialize sim_hello event: {err}"),
    }
}

fn queue_backend_food_snapshot_event(
    config: Res<BackendClientConfig>,
    mut pending: ResMut<PendingBackendEvents>,
    mut sequence: ResMut<BackendSequence>,
    mut connection: NonSendMut<BackendConnectionState>,
    food_query: Query<(Entity, &Transform), (With<Food>, Without<ChildOf>)>,
) {
    if !config.is_enabled() || !connection.hello_queued || connection.food_snapshot_queued {
        return;
    }

    let foods = food_query
        .iter()
        .map(|(entity, transform)| StartupFoodPayload {
            food_id: entity.to_bits().to_string(),
            x: transform.translation.x,
            y: transform.translation.y,
        })
        .collect();

    let envelope = EventEnvelope {
        event_type: "sim_food_snapshot",
        sim_id: config.sim_id.clone(),
        seq: next_sequence(&mut sequence),
        timestamp_ms: 0,
        payload: FoodSnapshotPayload { foods },
    };

    match serde_json::to_string(&envelope) {
        Ok(json) => {
            pending.push(json);
            connection.food_snapshot_queued = true;
        }
        Err(err) => error!("Failed to serialize sim_food_snapshot event: {err}"),
    }
}

fn collect_backend_events(
    config: Res<BackendClientConfig>,
    mut pending: ResMut<PendingBackendEvents>,
    mut sequence: ResMut<BackendSequence>,
    mut events: MessageReader<BackendSimEvent>,
) {
    if !config.is_enabled() {
        return;
    }

    for event in events.read() {
        let serialized = match event {
            BackendSimEvent::FoodPickup {
                ant_id,
                food_id,
                x,
                y,
                direction_x,
                direction_y,
                frame,
            } => serde_json::to_string(&EventEnvelope {
                event_type: "food_pickup",
                sim_id: config.sim_id.clone(),
                seq: next_sequence(&mut sequence),
                timestamp_ms: 0,
                payload: FoodEventPayload {
                    ant_id: ant_id.clone(),
                    food_id: food_id.clone(),
                    x: *x,
                    y: *y,
                    direction_x: *direction_x,
                    direction_y: *direction_y,
                    frame: *frame,
                },
            }),
            BackendSimEvent::FoodDrop {
                ant_id,
                food_id,
                x,
                y,
                direction_x,
                direction_y,
                frame,
            } => serde_json::to_string(&EventEnvelope {
                event_type: "food_drop",
                sim_id: config.sim_id.clone(),
                seq: next_sequence(&mut sequence),
                timestamp_ms: 0,
                payload: FoodEventPayload {
                    ant_id: ant_id.clone(),
                    food_id: food_id.clone(),
                    x: *x,
                    y: *y,
                    direction_x: *direction_x,
                    direction_y: *direction_y,
                    frame: *frame,
                },
            }),
            BackendSimEvent::AntTurnMove {
                ant_id,
                x,
                y,
                direction_x,
                direction_y,
                frame,
            } => serde_json::to_string(&EventEnvelope {
                event_type: "ant_turn_move",
                sim_id: config.sim_id.clone(),
                seq: next_sequence(&mut sequence),
                timestamp_ms: 0,
                payload: TurnMovePayload {
                    ant_id: ant_id.clone(),
                    x: *x,
                    y: *y,
                    direction_x: *direction_x,
                    direction_y: *direction_y,
                    frame: *frame,
                },
            }),
        };

        match serialized {
            Ok(json) => pending.push(json),
            Err(err) => error!("Failed to serialize backend event: {err}"),
        }
    }
}

fn flush_backend_events(
    config: Res<BackendClientConfig>,
    mut pending: ResMut<PendingBackendEvents>,
    mut connection: NonSendMut<BackendConnectionState>,
) {
    if !config.is_enabled() {
        return;
    }

    if connection.sender.is_none() {
        let Some(url) = &config.url else {
            return;
        };

        match ewebsock::connect(url.clone(), Options::default()) {
            Ok((sender, receiver)) => {
                connection.sender = Some(sender);
                connection.receiver = Some(receiver);
                connection.opened = false;
            }
            Err(err) => {
                warn!("Failed to connect backend websocket client: {err}");
                return;
            }
        }
    }

    loop {
        let Some(event) = connection.receiver.as_ref().and_then(|receiver| receiver.try_recv()) else {
            break;
        };

        match event {
            WsEvent::Opened => connection.opened = true,
            WsEvent::Closed => {
                connection.sender = None;
                connection.receiver = None;
                connection.opened = false;
                return;
            }
            WsEvent::Error(err) => warn!("Backend websocket error: {err}"),
            WsEvent::Message(_) => {}
        }
    }

    if !connection.opened {
        return;
    }

    if let Some(sender) = &mut connection.sender {
        while let Some(message) = pending.pop() {
            sender.send(WsMessage::Text(message));
        }
    }
}

fn next_sequence(sequence: &mut BackendSequence) -> u64 {
    sequence.0 += 1;
    sequence.0
}
