use std::{net::SocketAddr, time::Duration};

use an_gatherers::collision::{initialize_hittables, update_hittable_positions};
use an_gatherers::spatial_index::SpatialIndex;
use an_gatherers::*;
use axum::serve;
use bevy::prelude::*;
use gatherers_backend_rust::app::{AppState, build_router_with_state};
use tokio::{net::TcpListener, task::JoinHandle, time::sleep};

#[tokio::test(flavor = "multi_thread", worker_threads = 2)]
async fn bevy_client_plugin_sends_events_to_running_rust_backend() {
    let state = AppState::new();
    let server = spawn_test_server(state.clone()).await;

    let mut app = App::new();
    app.add_plugins(MinimalPlugins)
        .init_resource::<SpatialIndex>()
        .add_message::<HitEvent<Food, Ant>>()
        .insert_resource(SimulationSettings::default())
        .insert_resource(BackendClientConfig::enabled(
            server.ingest_ws_url(),
            "sim-e2e".to_string(),
        ))
        .add_plugins(BackendClientPlugin)
        .add_systems(Startup, initialize_hittables::<Food>)
        .add_systems(Update, update_hittable_positions::<Food>)
        .add_systems(Update, gatherer_movement)
        .add_systems(
            Update,
            (debug_collision_system::<Food, Ant>, ant_hits_system)
                .chain()
                .after(gatherer_movement),
        );

    let world = app.world_mut();
    let ant = world
        .spawn((
            Ant,
            Velocity(Vec2::new(1.0, 0.0)),
            Transform::from_translation(Vec3::new(0.0, 0.0, 0.0)),
            Bounding::from_radius(10.0),
            Collidable,
        ))
        .id();
    let food = world
        .spawn((
            Food,
            Transform::from_translation(Vec3::new(5.0, 0.0, 0.0)),
            Bounding::from_radius(5.0),
            Collidable,
        ))
        .id();
    world
        .resource_mut::<SpatialIndex>()
        .update(food, Vec2::new(5.0, 0.0));

    let mut last_snapshot = state.current_snapshot();
    let mut observed = false;
    for _ in 0..100 {
        app.update();
        sleep(Duration::from_millis(20)).await;

        last_snapshot = state.current_snapshot();
        if last_snapshot.summary.live_summary.connected_sim_count == 1
            && last_snapshot.summary.live_summary.loose_food_count == 0
            && last_snapshot.sims.iter().any(|sim| {
                sim.sim_id == "sim-e2e"
                    && sim.ant_count == 1
                    && sim.pickup_count == 1
                    && sim.drop_count == 0
                    && sim.turn_move_count == 1
                    && sim.loose_food_count == 0
            })
        {
            observed = true;
            break;
        }
    }

    assert!(
        observed,
        "expected real Bevy client websocket traffic to reach running Rust backend; ant={ant:?}, last_snapshot={last_snapshot:?}"
    );

    server.abort();
}

struct TestServer {
    addr: SocketAddr,
    task: JoinHandle<()>,
}

impl TestServer {
    fn ingest_ws_url(&self) -> String {
        format!("ws://{}/ws/ingest", self.addr)
    }

    fn abort(self) {
        self.task.abort();
    }
}

async fn spawn_test_server(state: AppState) -> TestServer {
    let router = build_router_with_state(state);
    let listener = TcpListener::bind("127.0.0.1:0")
        .await
        .expect("test listener should bind");
    let addr = listener.local_addr().expect("listener addr");
    let task = tokio::spawn(async move {
        serve(listener, router)
            .await
            .expect("test server should run");
    });

    TestServer { addr, task }
}
