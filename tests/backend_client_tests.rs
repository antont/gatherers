use an_gatherers::collision::{initialize_hittables, update_hittable_positions};
use an_gatherers::spatial_index::SpatialIndex;
use an_gatherers::*;
use bevy::prelude::*;

#[test]
fn test_pickup_collision_queues_backend_messages() {
    let mut app = App::new();

    app.add_plugins(MinimalPlugins)
        .init_resource::<SpatialIndex>()
        .add_message::<HitEvent<Food, Ant>>()
        .insert_resource(SimulationSettings::default())
        .insert_resource(BackendClientConfig::enabled(
            "ws://localhost:8080/ws/ingest".to_string(),
            "sim-test".to_string(),
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

    app.update();

    let queued = app
        .world()
        .resource::<PendingBackendEvents>()
        .queued_json_messages();

    assert!(
        queued.iter().any(|msg| msg.contains("\"type\":\"food_pickup\"")),
        "expected a food_pickup backend message after collision, got {:?}",
        queued
    );
    assert!(
        queued.iter().any(|msg| msg.contains("\"type\":\"ant_turn_move\"")),
        "expected an ant_turn_move backend message after collision, got {:?}",
        queued
    );
    assert!(
        queued.iter().all(|msg| msg.contains("\"sim_id\":\"sim-test\"")),
        "expected queued backend messages to include the configured sim_id, got {:?}",
        queued
    );

    let has_children = app
        .world()
        .entity(ant)
        .get::<Children>()
        .is_some_and(|children| !children.is_empty());
    assert!(has_children, "expected ant to pick up food in the simulation too");
}

#[test]
fn test_startup_queues_food_snapshot_after_hello() {
    let mut app = App::new();

    app.add_plugins(MinimalPlugins)
        .insert_resource(BackendClientConfig::enabled(
            "ws://localhost:8080/ws/ingest".to_string(),
            "sim-startup".to_string(),
        ))
        .add_plugins(BackendClientPlugin);

    let world = app.world_mut();
    world.spawn((
        Food,
        Transform::from_translation(Vec3::new(10.0, 20.0, 0.0)),
    ));
    world.spawn((
        Food,
        Transform::from_translation(Vec3::new(30.0, 40.0, 0.0)),
    ));

    app.update();

    let queued = app
        .world()
        .resource::<PendingBackendEvents>()
        .queued_json_messages();

    assert!(
        queued.len() >= 2,
        "expected sim_hello and sim_food_snapshot startup messages, got {:?}",
        queued
    );
    assert!(
        queued[0].contains("\"type\":\"sim_hello\""),
        "expected first startup message to be sim_hello, got {:?}",
        queued
    );
    assert!(
        queued[1].contains("\"type\":\"sim_food_snapshot\""),
        "expected second startup message to be sim_food_snapshot, got {:?}",
        queued
    );
    assert!(
        queued[1].contains("\"food_id\""),
        "expected startup food snapshot to include food ids, got {:?}",
        queued
    );
    assert!(
        queued[1].contains("\"x\":10.0") && queued[1].contains("\"y\":20.0"),
        "expected startup food snapshot to include first food position, got {:?}",
        queued
    );
    assert!(
        queued[1].contains("\"x\":30.0") && queued[1].contains("\"y\":40.0"),
        "expected startup food snapshot to include second food position, got {:?}",
        queued
    );
}
