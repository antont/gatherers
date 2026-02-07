use an_gatherers::collision::{initialize_hittables, update_hittable_positions};
use an_gatherers::spatial_index::SpatialIndex;
use an_gatherers::*;
use bevy::prelude::*;
use bevy::time::TimeUpdateStrategy;

// Test event collector to capture simulation events
#[derive(Resource, Default)]
struct TestEventCollector {
    collisions: Vec<CollisionEvent>,
    ant_positions: Vec<AntPositionEvent>,
    pickup_events: Vec<PickupEvent>,
}

#[derive(Debug, Clone)]
struct CollisionEvent {
    frame: u64,
    ant_id: Entity,
    food_id: Entity,
    ant_pos: Vec3,
    food_pos: Vec3,
}

#[derive(Debug, Clone)]
struct AntPositionEvent {
    frame: u64,
    ant_id: Entity,
    position: Vec3,
    has_cooldown: bool,
}

#[derive(Debug, Clone)]
struct PickupEvent {
    frame: u64,
    ant_id: Entity,
    food_id: Entity,
    action: String, // "pickup" or "drop"
}

// Test systems to collect events
fn collect_collision_events(
    mut collector: ResMut<TestEventCollector>,
    mut collision_events: MessageReader<HitEvent<Food, Ant>>,
    ant_query: Query<&Transform, With<Ant>>,
    food_query: Query<&Transform, (With<Food>, Without<Ant>)>,
    frame_count: Res<FrameCount>,
) {
    for event in collision_events.read() {
        let ant_id = event.hitter();
        let food_id = event.hittable();

        println!(
            "Collision detected at frame {}: ant {:?} with food {:?}",
            frame_count.0, ant_id, food_id
        );

        if let (Ok(ant_transform), Ok(food_transform)) =
            (ant_query.get(ant_id), food_query.get(food_id))
        {
            collector.collisions.push(CollisionEvent {
                frame: frame_count.0,
                ant_id,
                food_id,
                ant_pos: ant_transform.translation,
                food_pos: food_transform.translation,
            });
        }
    }
}

fn collect_ant_positions(
    mut collector: ResMut<TestEventCollector>,
    ant_query: Query<(Entity, &Transform, Option<&Cooldown>), With<Ant>>,
    frame_count: Res<FrameCount>,
) {
    for (ant_id, transform, cooldown) in ant_query.iter() {
        collector.ant_positions.push(AntPositionEvent {
            frame: frame_count.0,
            ant_id,
            position: transform.translation,
            has_cooldown: cooldown.is_some(),
        });
    }
}

fn collect_pickup_events(
    mut collector: ResMut<TestEventCollector>,
    mut collision_events: MessageReader<HitEvent<Food, Ant>>,
    ant_query: Query<&Children, With<Ant>>,
    frame_count: Res<FrameCount>,
) {
    for event in collision_events.read() {
        let ant_id = event.hitter();
        let food_id = event.hittable();

        // Determine if this is a pickup or drop by checking if ant has children
        let action = if let Ok(children) = ant_query.get(ant_id) {
            if children.is_empty() {
                "pickup".to_string()
            } else {
                "drop".to_string()
            }
        } else {
            "pickup".to_string() // Default assumption
        };

        collector.pickup_events.push(PickupEvent {
            frame: frame_count.0,
            ant_id,
            food_id,
            action,
        });
    }
}

// Helper to create a test simulation app
fn create_test_app() -> App {
    let mut app = App::new();

    app.add_plugins(MinimalPlugins)
        .init_resource::<SpatialIndex>()
        .add_message::<HitEvent<Food, Ant>>()
        .insert_resource(SimulationSettings::default())
        .insert_resource(TestEventCollector::default())
        .insert_resource(Time::<()>::default())
        .insert_resource(FrameCount::default())
        // Use fixed timestep for deterministic testing
        .insert_resource(TimeUpdateStrategy::ManualDuration(
            std::time::Duration::from_millis(16),
        )) // 60 FPS
        .add_systems(Startup, initialize_hittables::<Food>)
        .add_systems(Update, update_hittable_positions::<Food>)
        .add_systems(Update, frame_counter_system)
        .add_systems(Update, gatherer_movement)
        .add_systems(
            Update,
            (
                debug_collision_system::<Food, Ant>,
                ant_hits_system,
                collect_collision_events,
                collect_ant_positions,
                collect_pickup_events,
            )
                .after(gatherer_movement),
        )
        .add_systems(PostUpdate, cooldown_system);

    app
}

// Helper to spawn a simple test scenario
fn spawn_test_scenario(app: &mut App) -> (Entity, Entity) {
    let world = app.world_mut();

    // Spawn ant at origin moving right
    let ant = world
        .spawn((
            Ant,
            Velocity(Vec2::new(1.0, 0.0)), // Moving right
            Transform::from_translation(Vec3::new(-50.0, 0.0, 0.0)),
            Bounding::from_radius(10.0), // Collision radius
            Collidable,
        ))
        .id();

    // Spawn food to the right of ant
    let food = world
        .spawn((
            Food,
            Transform::from_translation(Vec3::new(50.0, 0.0, 0.0)),
            Bounding::from_radius(5.0), // Food collision radius
            Collidable,
        ))
        .id();

    // Manually update spatial index for food (since we spawned after startup)
    let food_pos = world
        .entity(food)
        .get::<Transform>()
        .unwrap()
        .translation
        .truncate();
    let mut spatial_index = world.resource_mut::<SpatialIndex>();
    spatial_index.update(food, food_pos);
    println!(
        "Added food entity {:?} at {:?} to spatial index",
        food, food_pos
    );

    (ant, food)
}

#[test]
fn test_spatial_index_debug() {
    let mut app = create_test_app();

    let world = app.world_mut();
    let mut spatial_index = world.resource_mut::<SpatialIndex>();

    // Test spatial index directly
    let food_entity = Entity::from_raw(999); // Dummy entity
    spatial_index.update(food_entity, Vec2::new(50.0, 0.0));

    let nearby_at_food = spatial_index.get_nearby(Vec2::new(50.0, 0.0));
    let nearby_at_ant = spatial_index.get_nearby(Vec2::new(-50.0, 0.0));
    let nearby_close = spatial_index.get_nearby(Vec2::new(35.0, 0.0)); // Should find food

    println!("Food at (50,0): {} nearby entities", nearby_at_food.len());
    println!("Ant at (-50,0): {} nearby entities", nearby_at_ant.len());
    println!(
        "Close to food (35,0): {} nearby entities",
        nearby_close.len()
    );

    // Check cell calculations
    let cell_size = 20.0f32; // From Config::SPATIAL_CELL_SIZE
    let food_cell = (50.0f32 / cell_size).floor() as i32;
    let ant_cell = (-50.0f32 / cell_size).floor() as i32;
    let close_cell = (35.0f32 / cell_size).floor() as i32;

    println!(
        "Food cell X: {}, Ant cell X: {}, Close cell X: {}",
        food_cell, ant_cell, close_cell
    );

    assert!(
        nearby_at_food.len() > 0,
        "Should find food entity at food position"
    );
    assert!(nearby_close.len() > 0, "Should find food entity when close");
}

#[test]
fn test_collision_system_basic_debug() {
    let mut app = create_test_app();

    // Create ant and food very close together to guarantee collision
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
            Transform::from_translation(Vec3::new(5.0, 0.0, 0.0)), // Very close!
            Bounding::from_radius(5.0),
            Collidable,
        ))
        .id();

    // Update spatial index
    let mut spatial_index = world.resource_mut::<SpatialIndex>();
    spatial_index.update(food, Vec2::new(5.0, 0.0));
    drop(spatial_index);
    drop(world);

    println!("Initial setup: ant at (0,0), food at (5,0), should collide immediately");

    // Run one frame
    app.update();

    let collector = app.world().resource::<TestEventCollector>();
    println!(
        "After 1 frame: {} collision events detected",
        collector.collisions.len()
    );

    // Check positions
    let world = app.world();
    let ant_pos = world.entity(ant).get::<Transform>().unwrap().translation;
    let food_pos = world.entity(food).get::<Transform>().unwrap().translation;
    let distance = (ant_pos - food_pos).length();

    println!(
        "Ant at {:?}, food at {:?}, distance: {:.2}",
        ant_pos, food_pos, distance
    );

    assert!(
        !collector.collisions.is_empty(),
        "Expected collision with entities 5 units apart"
    );
}

#[test]
fn test_ant_should_collide_with_food() {
    let mut app = create_test_app();
    let (ant_id, food_id) = spawn_test_scenario(&mut app);

    // Run simulation for enough frames for ant to reach food
    for _ in 0..100 {
        app.update();
    }

    let collector = app.world().resource::<TestEventCollector>();

    // Should have at least one collision
    assert!(
        !collector.collisions.is_empty(),
        "Expected at least one collision"
    );

    // Find collision between our specific ant and food
    let collision = collector
        .collisions
        .iter()
        .find(|c| c.ant_id == ant_id && c.food_id == food_id);

    assert!(
        collision.is_some(),
        "Expected collision between test ant and food"
    );

    println!(
        "Test collision found at frame {}: ant at {:?}, food at {:?}",
        collision.unwrap().frame,
        collision.unwrap().ant_pos,
        collision.unwrap().food_pos
    );
}

#[test]
fn test_ant_with_cooldown_should_not_pickup_food() {
    let mut app = create_test_app();
    let (ant_id, _) = spawn_test_scenario(&mut app);

    // Manually add cooldown to ant
    let world = app.world_mut();
    world.entity_mut(ant_id).insert(Cooldown {
        distance_squared_remaining: 10000.0, // Large cooldown
        last_position: Vec3::new(-50.0, 0.0, 0.0),
    });

    // Run simulation
    for _ in 0..100 {
        app.update();
    }

    let collector = app.world().resource::<TestEventCollector>();

    // Should have no pickup events for this ant
    let pickup_events: Vec<_> = collector
        .pickup_events
        .iter()
        .filter(|p| p.ant_id == ant_id)
        .collect();

    assert!(
        pickup_events.is_empty(),
        "Ant with cooldown should not pickup food, but found {} pickup events",
        pickup_events.len()
    );
}

#[test]
fn test_collision_tunneling_at_slow_speed() {
    let mut app = create_test_app();

    // Set very slow speed
    app.world_mut()
        .resource_mut::<SimulationSettings>()
        .speed_multiplier = 0.1;

    let (ant_id, food_id) = spawn_test_scenario(&mut app);

    // Run for many frames to ensure ant has time to reach food
    for i in 0..500 {
        app.update();

        // Check ant position every 50 frames
        if i % 50 == 0 {
            let world = app.world();
            let ant_transform = world.entity(ant_id).get::<Transform>().unwrap();
            let food_transform = world.entity(food_id).get::<Transform>().unwrap();
            let ant_bounding = world.entity(ant_id).get::<Bounding>().unwrap();
            let food_bounding = world.entity(food_id).get::<Bounding>().unwrap();

            let distance = (ant_transform.translation - food_transform.translation).length();
            let collision_distance = **ant_bounding + **food_bounding;
            let settings = world.resource::<SimulationSettings>();
            let max_movement = settings.max_movement_per_frame();

            println!(
                "Frame {}: ant at {:?}, food at {:?}, distance: {:.2}, collision_distance: {:.2}, max_movement: {:.2}",
                i, ant_transform.translation, food_transform.translation, distance, collision_distance, max_movement
            );
        }
    }

    let collector = app.world().resource::<TestEventCollector>();

    // Should have at least one collision
    assert!(
        !collector.collisions.is_empty(),
        "Expected collision at slow speed, but found none. Ant may have tunneled through food."
    );

    // Verify collision happened when ant was close to food
    for collision in &collector.collisions {
        let distance = (collision.ant_pos - collision.food_pos).length();
        println!(
            "Collision at frame {}: distance = {:.2}",
            collision.frame, distance
        );

        // Distance should be within collision radius
        let expected_collision_radius = 15.0; // ant radius + food radius
        assert!(distance <= expected_collision_radius,
               "Collision detected at distance {:.2}, but should be within {:.2}. This suggests incorrect collision detection.",
               distance, expected_collision_radius);
    }
}

#[test]
fn test_cooldown_duration_consistency_across_speeds() {
    let speeds = vec![0.1, 0.5, 1.0, 2.0, 5.0];

    for speed in speeds {
        let mut app = create_test_app();
        app.world_mut()
            .resource_mut::<SimulationSettings>()
            .speed_multiplier = speed;

        let (ant_id, _) = spawn_test_scenario(&mut app);

        // Manually trigger cooldown
        let cooldown_distance_sq = app
            .world()
            .resource::<SimulationSettings>()
            .pickup_cooldown_distance_squared();

        app.world_mut().entity_mut(ant_id).insert(Cooldown {
            distance_squared_remaining: cooldown_distance_sq,
            last_position: Vec3::new(-50.0, 0.0, 0.0),
        });

        let mut cooldown_frames = 0;

        // Count how many frames until cooldown is removed
        for _frame in 0..1000 {
            app.update();

            let world = app.world();
            let has_cooldown = world.entity(ant_id).get::<Cooldown>().is_some();

            if has_cooldown {
                cooldown_frames += 1;
            } else {
                break;
            }
        }

        let time_per_frame = 1.0 / 60.0; // 60 FPS
        let cooldown_duration = cooldown_frames as f32 * time_per_frame;

        println!(
            "Speed {:.1}x: cooldown lasted {} frames ({:.2} seconds)",
            speed, cooldown_frames, cooldown_duration
        );

        // Cooldown should last approximately 1 second regardless of speed
        assert!(
            (cooldown_duration - 1.0).abs() < 0.2,
            "Cooldown duration {:.2}s at speed {:.1}x should be close to 1.0s",
            cooldown_duration,
            speed
        );
    }
}
