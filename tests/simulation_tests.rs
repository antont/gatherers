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

const DEFAULT_FRAME_MS: u64 = 16;

fn create_test_app_with_dt(frame_ms: u64) -> App {
    let mut app = App::new();

    app.add_plugins(MinimalPlugins)
        .init_resource::<SpatialIndex>()
        .add_message::<HitEvent<Food, Ant>>()
        .insert_resource(SimulationSettings::default())
        .insert_resource(TestEventCollector::default())
        .insert_resource(Time::<()>::default())
        .insert_resource(FrameCount::default())
        .insert_resource(TimeUpdateStrategy::ManualDuration(
            std::time::Duration::from_millis(frame_ms),
        ))
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

fn create_test_app() -> App {
    create_test_app_with_dt(DEFAULT_FRAME_MS)
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
    let food_entity = Entity::from_bits(999); // Dummy entity
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

    // Manually add cooldown to ant with large timer so it won't expire during the test
    let world = app.world_mut();
    world.entity_mut(ant_id).insert(Cooldown {
        timer: 10000.0,
    });

    // Run simulation long enough for ant to pass through food's location
    for _ in 0..200 {
        app.update();
    }

    // Verify the ant did NOT actually pick up food (has no children)
    let world = app.world();
    let has_children = world
        .entity(ant_id)
        .get::<Children>()
        .is_some_and(|c| !c.is_empty());

    assert!(
        !has_children,
        "Ant with cooldown should not pick up food"
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
    // At 0.1x speed: 10 units/sec, 100 unit distance, ~10 seconds = ~625 frames at 60fps
    for i in 0..800 {
        app.update();

        // Check ant position every 100 frames
        if i % 100 == 0 {
            let world = app.world();
            let ant_transform = world.entity(ant_id).get::<Transform>().unwrap();
            let food_transform = world.entity(food_id).get::<Transform>().unwrap();
            let ant_bounding = world.entity(ant_id).get::<Bounding>().unwrap();
            let food_bounding = world.entity(food_id).get::<Bounding>().unwrap();

            let distance = (ant_transform.translation - food_transform.translation).length();
            let collision_distance = **ant_bounding + **food_bounding;
            let settings = world.resource::<SimulationSettings>();
            let max_movement = settings.safe_step_distance();

            println!(
                "Frame {}: ant at {:?}, food at {:?}, distance: {:.2}, collision_distance: {:.2}, max_movement: {:.2}",
                i, ant_transform.translation, food_transform.translation, distance, collision_distance, max_movement
            );
        }
    }

    let collector = app.world().resource::<TestEventCollector>();

    // Should have at least one collision (ant didn't tunnel through food)
    assert!(
        !collector.collisions.is_empty(),
        "Expected collision at slow speed, but found none. Ant may have tunneled through food."
    );

    println!(
        "Collision detected at frame {}",
        collector.collisions[0].frame
    );
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

        // Manually trigger cooldown with standard duration
        app.world_mut().entity_mut(ant_id).insert(Cooldown {
            timer: Config::BASE_PICKUP_COOLDOWN,
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

        let time_per_frame = DEFAULT_FRAME_MS as f32 / 1000.0;
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

// --- Regression tests: speed scaling ---

fn create_movement_test_app_with_dt(speed_multiplier: f32, frame_ms: u64) -> App {
    let mut app = App::new();
    app.add_plugins(MinimalPlugins)
        .insert_resource(SimulationSettings {
            speed_multiplier,
        })
        .insert_resource(Time::<()>::default())
        .insert_resource(TimeUpdateStrategy::ManualDuration(
            std::time::Duration::from_millis(frame_ms),
        ))
        .add_systems(Update, gatherer_movement);
    app
}

fn create_movement_test_app(speed_multiplier: f32) -> App {
    create_movement_test_app_with_dt(speed_multiplier, DEFAULT_FRAME_MS)
}

fn spawn_rightward_ant(app: &mut App) -> Entity {
    app.world_mut()
        .spawn((
            Ant,
            Velocity(Vec2::new(1.0, 0.0)),
            Transform::from_translation(Vec3::ZERO),
        ))
        .id()
}

const MOVEMENT_TEST_FRAMES: usize = 10;

fn measure_displacement(speed_multiplier: f32) -> f32 {
    measure_displacement_with_dt(speed_multiplier, DEFAULT_FRAME_MS)
}

fn measure_displacement_with_dt(speed_multiplier: f32, frame_ms: u64) -> f32 {
    let mut app = create_movement_test_app_with_dt(speed_multiplier, frame_ms);
    let ant = spawn_rightward_ant(&mut app);

    for _ in 0..MOVEMENT_TEST_FRAMES {
        app.update();
    }

    app.world()
        .entity(ant)
        .get::<Transform>()
        .unwrap()
        .translation
        .x
}

#[test]
fn test_speed_scaling_is_monotonic() {
    let speeds = [1.0_f32, 2.0, 5.0, 10.0];
    let mut displacements = Vec::new();

    for &speed in &speeds {
        let disp = measure_displacement(speed);
        displacements.push((speed, disp));
        println!("Speed {:.1}x: displacement = {:.4}", speed, disp);
    }

    for i in 1..displacements.len() {
        let (prev_speed, prev_disp) = displacements[i - 1];
        let (curr_speed, curr_disp) = displacements[i];
        assert!(
            curr_disp > prev_disp,
            "Speed {:.1}x displacement ({:.4}) should exceed {:.1}x displacement ({:.4})",
            curr_speed,
            curr_disp,
            prev_speed,
            prev_disp
        );
    }
}

#[test]
fn test_unlimited_speed_moves_forward_not_backward() {
    let disp = measure_displacement(Config::UNLIMITED_SPEED);

    println!("Unlimited speed: displacement = {:.4}", disp);

    assert!(
        disp > 0.0,
        "Unlimited speed should move in velocity direction (positive X), got {:.4}",
        disp
    );
}

// --- Frame-rate-aware speed tests ---

#[test]
fn test_unlimited_at_least_as_fast_as_max_normal_all_frame_rates() {
    for &dt_ms in &[8u64, 16, 33, 50] {
        let normal_disp = measure_displacement_with_dt(Config::MAX_SPEED_MULTIPLIER, dt_ms);
        let unlimited_disp = measure_displacement_with_dt(Config::UNLIMITED_SPEED, dt_ms);

        println!(
            "At {}ms: max normal = {:.2}, unlimited = {:.2}",
            dt_ms, normal_disp, unlimited_disp
        );

        assert!(
            unlimited_disp >= normal_disp - 0.1,
            "At {}ms, unlimited ({:.2}) should be >= max normal ({:.2})",
            dt_ms,
            unlimited_disp,
            normal_disp
        );
    }
}

#[test]
fn test_effective_speed_unlimited_adapts_to_frame_time() {
    let settings = SimulationSettings {
        speed_multiplier: Config::UNLIMITED_SPEED,
    };
    let safe_step = settings.safe_step_distance();

    for &dt_ms in &[8u64, 16, 33, 50] {
        let dt = dt_ms as f32 / 1000.0;
        let effective = settings.effective_speed(dt);
        let expected = safe_step / dt;

        println!(
            "At {}ms: effective_speed = {:.1}, expected = {:.1}",
            dt_ms, effective, expected
        );

        assert!(
            (effective - expected).abs() < 0.01,
            "At {}ms, effective_speed should be {:.1}, got {:.1}",
            dt_ms,
            expected,
            effective
        );
    }
}

#[test]
fn test_effective_speed_normal_unaffected_at_high_fps() {
    let settings = SimulationSettings {
        speed_multiplier: 5.0,
    };
    let dt = 0.016;
    let effective = settings.effective_speed(dt);
    let nominal = Config::BASE_ANT_SPEED * 5.0;

    assert!(
        (effective - nominal).abs() < 0.01,
        "At 60fps, 5x should not be capped: expected {}, got {}",
        nominal,
        effective
    );
}

#[test]
fn test_effective_multiplier_unlimited() {
    let settings = SimulationSettings {
        speed_multiplier: Config::UNLIMITED_SPEED,
    };

    let mult_60 = settings.effective_multiplier(0.016);
    println!("At 60fps: effective multiplier = {:.2}x", mult_60);
    assert!(
        mult_60 > 10.0,
        "Unlimited at 60fps should exceed 10x: got {:.1}x",
        mult_60
    );

    let mult_30 = settings.effective_multiplier(0.033);
    println!("At 30fps: effective multiplier = {:.2}x", mult_30);
    assert!(
        mult_30 > 5.0 && mult_30 < 6.0,
        "Unlimited at 30fps should be ~5.5x: got {:.1}x",
        mult_30
    );
}

#[test]
fn test_unlimited_displacement_equals_safe_step_per_frame() {
    let safe_step = SimulationSettings::default().safe_step_distance();

    for &dt_ms in &[8u64, 16, 33, 50] {
        let disp = measure_displacement_with_dt(Config::UNLIMITED_SPEED, dt_ms);
        let non_zero_frames = (MOVEMENT_TEST_FRAMES - 1) as f32;
        let per_frame = disp / non_zero_frames;

        println!(
            "At {}ms: per-frame displacement = {:.2}, safe_step = {:.2}",
            dt_ms, per_frame, safe_step
        );

        assert!(
            (per_frame - safe_step).abs() < 0.5,
            "At {}ms, unlimited should move {:.1} per frame, got {:.1}",
            dt_ms,
            safe_step,
            per_frame
        );
    }
}

#[test]
fn test_effective_speed_zero_delta_returns_zero() {
    let settings = SimulationSettings {
        speed_multiplier: Config::UNLIMITED_SPEED,
    };
    assert_eq!(settings.effective_speed(0.0), 0.0);
    assert_eq!(settings.effective_multiplier(0.0), 0.0);
}

// --- Regression test: unlimited speed must not tunnel through food ---

#[test]
fn test_unlimited_speed_no_tunneling() {
    let mut app = create_test_app();

    {
        let world = app.world_mut();
        world.insert_resource(SimulationSettings {
            speed_multiplier: Config::UNLIMITED_SPEED,
        });
    }

    let world = app.world_mut();

    let ant = world
        .spawn((
            Ant,
            Velocity(Vec2::new(1.0, 0.0)),
            Transform::from_translation(Vec3::new(-50.0, 0.0, Config::ANT_Z_LAYER)),
            Bounding::from_radius(10.0),
            Collidable,
        ))
        .id();

    let food = world
        .spawn((
            Food,
            Transform::from_translation(Vec3::new(50.0, 0.0, Config::FOOD_Z_LAYER)),
            Bounding::from_radius(5.0),
            Collidable,
        ))
        .id();

    let mut spatial_index = world.resource_mut::<SpatialIndex>();
    spatial_index.update(food, Vec2::new(50.0, 0.0));
    drop(spatial_index);

    for _ in 0..100 {
        app.update();
    }

    let ant_has_children = app
        .world()
        .entity(ant)
        .get::<Children>()
        .is_some_and(|c| !c.is_empty());

    let ant_pos = app
        .world()
        .entity(ant)
        .get::<Transform>()
        .unwrap()
        .translation;

    println!(
        "Unlimited speed: ant at {:?}, carrying food: {}",
        ant_pos, ant_has_children
    );

    assert!(
        ant_has_children,
        "Ant should have picked up food at unlimited speed (not tunneled through it). \
         Ant ended at {:?}",
        ant_pos
    );
}

#[test]
fn test_unlimited_speed_no_tunneling_at_low_fps() {
    let mut app = create_test_app_with_dt(50);

    {
        let world = app.world_mut();
        world.insert_resource(SimulationSettings {
            speed_multiplier: Config::UNLIMITED_SPEED,
        });
    }

    let world = app.world_mut();

    let ant = world
        .spawn((
            Ant,
            Velocity(Vec2::new(1.0, 0.0)),
            Transform::from_translation(Vec3::new(-50.0, 0.0, Config::ANT_Z_LAYER)),
            Bounding::from_radius(10.0),
            Collidable,
        ))
        .id();

    let food = world
        .spawn((
            Food,
            Transform::from_translation(Vec3::new(50.0, 0.0, Config::FOOD_Z_LAYER)),
            Bounding::from_radius(5.0),
            Collidable,
        ))
        .id();

    let mut spatial_index = world.resource_mut::<SpatialIndex>();
    spatial_index.update(food, Vec2::new(50.0, 0.0));
    drop(spatial_index);

    for _ in 0..200 {
        app.update();
    }

    let ant_has_children = app
        .world()
        .entity(ant)
        .get::<Children>()
        .is_some_and(|c| !c.is_empty());

    assert!(
        ant_has_children,
        "Ant should pick up food at unlimited speed even at 20fps (50ms frames)"
    );
}

// --- Regression tests: food lifecycle ---

#[test]
fn test_food_entity_count_preserved() {
    let mut app = create_test_app();

    let world = app.world_mut();
    let mut food_ids = Vec::new();

    // Spawn 5 food items in a line
    let _spatial_index = world.resource_mut::<SpatialIndex>();
    drop(_spatial_index);

    for i in 0..5 {
        let x = (i as f32) * 30.0;
        let food = world
            .spawn((
                Food,
                Transform::from_translation(Vec3::new(x, 0.0, 0.0)),
                Bounding::from_radius(5.0),
                Collidable,
            ))
            .id();
        food_ids.push((food, x));
    }

    let mut spatial_index = world.resource_mut::<SpatialIndex>();
    for &(food, x) in &food_ids {
        spatial_index.update(food, Vec2::new(x, 0.0));
    }
    drop(spatial_index);

    // Spawn an ant that will collide with the food
    world.spawn((
        Ant,
        Velocity(Vec2::new(1.0, 0.0)),
        Transform::from_translation(Vec3::new(-10.0, 0.0, 0.0)),
        Bounding::from_radius(10.0),
        Collidable,
    ));

    let initial_food_count = food_ids.len();

    // Run enough frames for ant to interact with food
    for _ in 0..200 {
        app.update();
    }

    // Count remaining food entities using a query
    let remaining_food: Vec<Entity> = app
        .world_mut()
        .query_filtered::<Entity, With<Food>>()
        .iter(app.world())
        .collect();

    println!(
        "Started with {} food, {} remaining after 200 frames",
        initial_food_count,
        remaining_food.len()
    );

    assert_eq!(
        remaining_food.len(),
        initial_food_count,
        "Food entities must never be despawned"
    );
}

#[test]
fn test_dropped_food_position_near_ant() {
    let mut app = create_test_app();

    let world = app.world_mut();

    // Ant already carrying food1 (manually set up the parent-child relationship)
    let ant = world
        .spawn((
            Ant,
            Velocity(Vec2::new(1.0, 0.0)),
            Transform::from_translation(Vec3::new(100.0, 50.0, 0.0)),
            Bounding::from_radius(10.0),
            Collidable,
        ))
        .id();

    let food1 = world
        .spawn((
            Food,
            Transform::from_translation(Vec3::new(0.0, 0.0, 0.0)),
            Bounding::from_radius(5.0),
        ))
        .id();

    // Attach food1 as child of ant (simulating a previous pickup)
    world.entity_mut(ant).add_child(food1);

    // Place food2 directly ahead of the ant to force a drop
    let food2 = world
        .spawn((
            Food,
            Transform::from_translation(Vec3::new(105.0, 50.0, 0.0)),
            Bounding::from_radius(5.0),
            Collidable,
        ))
        .id();

    let mut spatial_index = world.resource_mut::<SpatialIndex>();
    spatial_index.update(food2, Vec2::new(105.0, 50.0));
    drop(spatial_index);

    // Run enough frames for ant to reach food2 and trigger drop
    for _ in 0..50 {
        app.update();
    }

    // Check that the drop happened
    let ant_has_children = app
        .world()
        .entity(ant)
        .get::<Children>()
        .is_some_and(|c| !c.is_empty());

    let food1_pos = app
        .world()
        .entity(food1)
        .get::<Transform>()
        .unwrap()
        .translation;

    println!(
        "Food1 world position after drop: {:?}, ant still carrying: {}",
        food1_pos, ant_has_children
    );

    // The drop should have happened
    assert!(
        !ant_has_children,
        "Ant should have dropped food1 after hitting food2"
    );

    // Dropped food should be near where the ant was (~100,50), not at origin (0,0)
    let dist_from_origin = Vec2::new(food1_pos.x, food1_pos.y).length();
    assert!(
        dist_from_origin > 20.0,
        "Dropped food should be near ant's drop position, not at origin. Position: {:?}, distance from origin: {:.1}",
        food1_pos,
        dist_from_origin
    );
}

#[test]
fn test_dropped_food_regains_collidable() {
    let mut app = create_test_app();

    let world = app.world_mut();

    // Ant already carrying food1
    let ant = world
        .spawn((
            Ant,
            Velocity(Vec2::new(1.0, 0.0)),
            Transform::from_translation(Vec3::new(0.0, 0.0, 0.0)),
            Bounding::from_radius(10.0),
            Collidable,
        ))
        .id();

    let food1 = world
        .spawn((
            Food,
            Transform::from_translation(Vec3::new(0.0, 0.0, 0.0)),
            Bounding::from_radius(5.0),
            // No Collidable - it's been "picked up"
        ))
        .id();

    world.entity_mut(ant).add_child(food1);

    // Place food2 right ahead to trigger drop
    let food2 = world
        .spawn((
            Food,
            Transform::from_translation(Vec3::new(5.0, 0.0, 0.0)),
            Bounding::from_radius(5.0),
            Collidable,
        ))
        .id();

    let mut spatial_index = world.resource_mut::<SpatialIndex>();
    spatial_index.update(food2, Vec2::new(5.0, 0.0));
    drop(spatial_index);

    // Run frames to trigger collision and drop
    for _ in 0..20 {
        app.update();
    }

    let ant_carrying = app
        .world()
        .entity(ant)
        .get::<Children>()
        .is_some_and(|c| !c.is_empty());

    let food1_collidable = app.world().entity(food1).get::<Collidable>().is_some();

    println!(
        "After drop: ant carrying={}, food1 collidable={}",
        ant_carrying, food1_collidable
    );

    assert!(!ant_carrying, "Ant should have dropped food after hitting food2");
    assert!(
        food1_collidable,
        "Dropped food must regain Collidable component"
    );
}
