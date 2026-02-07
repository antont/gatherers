// Library interface for testing
pub mod boundary;
pub mod collision;
pub mod config;
pub mod spatial_index;
pub mod ui;

// Re-export main simulation components for testing
use bevy::prelude::*;
pub use boundary::Bounding;
pub use collision::{Collidable, CollisionPlugin, HitEvent};
pub use config::{Config, SimulationSettings};
use rand::Rng;
pub use spatial_index::SpatialIndex;

// Frame counter resource for tests
#[derive(Resource, Default)]
pub struct FrameCount(pub u64);

#[derive(Debug, Component)]
pub struct Velocity(pub Vec2);

#[derive(Debug, Component, Default)]
pub struct Ant;

#[derive(Debug, Component, Default)]
pub struct Food;

#[derive(Debug, Component)]
pub struct Cooldown {
    pub distance_squared_remaining: f32,
    pub last_position: Vec3,
}

// Debug collision system for tests
pub fn debug_collision_system<A: Component, B: Component>(
    mut hits: MessageWriter<HitEvent<A, B>>,
    spatial_index: Res<SpatialIndex>,
    hittables: Query<(Entity, &Transform, &Bounding), (With<Collidable>, With<A>)>,
    hitters: Query<(Entity, &Transform, &Bounding), (With<Collidable>, With<B>)>,
) {
    println!("Debug collision system running:");
    println!("  Hittables count: {}", hittables.iter().count());
    println!("  Hitters count: {}", hitters.iter().count());

    for (hitter_entity, hitter_transform, hitter_bounds) in hitters.iter() {
        let pos = hitter_transform.translation.truncate();
        println!(
            "  Checking hitter {:?} at {:?}",
            hitter_entity, hitter_transform.translation
        );

        // Debug spatial cell calculation
        let cell_x = (pos.x / 20.0).floor() as i32;
        let cell_y = (pos.y / 20.0).floor() as i32;
        println!("    Hitter in spatial cell ({}, {})", cell_x, cell_y);

        // Use spatial index to efficiently filter potential collisions to nearby entities only
        let nearby_entities = spatial_index.get_nearby(pos);
        println!(
            "    Nearby entities from spatial index: {} entities",
            nearby_entities.len()
        );

        for &nearby_entity in nearby_entities.iter() {
            println!("      Checking nearby entity {:?}", nearby_entity);

            // Actually check distance - spatial index just provides nearby candidates
            if let Ok((hittable_entity, hittable_transform, hittable_bounds)) =
                hittables.get(nearby_entity)
            {
                let distance_squared = (hittable_transform.translation
                    - hitter_transform.translation)
                    .length_squared();
                let collision_distance_squared = (**hittable_bounds + **hitter_bounds).powi(2);

                println!(
                    "        Distance²: {:.2}, collision distance²: {:.2}",
                    distance_squared, collision_distance_squared
                );

                if distance_squared < collision_distance_squared {
                    println!("        COLLISION DETECTED!");
                    hits.write(HitEvent::new(hittable_entity, hitter_entity));
                    // Only handle one collision per frame - simulates realistic ant behavior
                    // where an ant deals with one food item at a time
                    break;
                } else {
                    println!("        No collision - too far");
                }
            } else {
                println!(
                    "        Entity {:?} not found in hittables query",
                    nearby_entity
                );
            }
        }
    }
}

// Simulation systems - extracted from main.rs for testing
pub fn gatherer_movement(
    time: Res<Time>,
    settings: Res<SimulationSettings>,
    mut sprite_position: Query<(&Velocity, &mut Transform), With<Ant>>,
) {
    let max_movement = settings.max_movement_per_frame();
    let delta_time = time.delta_secs();

    // Calculate current speed based on performance and mode
    let current_speed = if settings.is_unlimited_speed() {
        // For unlimited mode: use a reasonable high speed but not infinite
        settings.ant_speed() * 20.0 // 20x faster than max normal speed
    } else {
        settings.ant_speed()
    };

    for (velocity, mut transform) in &mut sprite_position {
        // Use velocity direction (normalized) * current speed setting
        let direction = velocity.0.normalize_or_zero();
        let actual_velocity = direction * current_speed;
        let desired_movement = actual_velocity * delta_time;

        // Limit movement distance to prevent tunneling through food
        let movement_distance = desired_movement.length();
        let final_movement = if movement_distance > max_movement {
            // Scale down movement to maximum allowed distance
            desired_movement.normalize() * max_movement
        } else {
            desired_movement
        };

        transform.translation.x += final_movement.x;
        transform.translation.y += final_movement.y;
    }
}

pub fn ant_hits_system(
    mut ant_hits: MessageReader<HitEvent<Food, Ant>>,
    mut commands: Commands,
    mut ant_query: Query<
        (&mut Velocity, Option<&Children>, &Transform),
        (With<Ant>, Without<Cooldown>),
    >,
    mut food_query: Query<&mut Transform, (With<Food>, Without<Ant>)>,
    settings: Res<SimulationSettings>,
) {
    let mut rng = rand::rng();

    for hit in ant_hits.read() {
        let ant = hit.hitter();
        let food = hit.hittable();

        if let Ok((mut velocity, carrying, ant_transform)) = ant_query.get_mut(ant) {
            if let Some(carrying) = carrying {
                if !carrying.is_empty() {
                    let carried_food = carrying[0];
                    commands.entity(carried_food).remove::<ChildOf>();
                    commands.entity(carried_food).insert(Collidable);
                    commands.entity(ant).insert(Cooldown {
                        distance_squared_remaining: settings.pickup_cooldown_distance_squared(),
                        last_position: ant_transform.translation,
                    });

                    // Add direction randomization when dropping food (same as pickup behavior)
                    let current_angle = velocity.0.angle_to(Vec2::new(1.0, 0.0));
                    let angle = current_angle
                        + std::f32::consts::PI
                        + rng.random_range(-Config::TURN_ANGLE_RANGE..Config::TURN_ANGLE_RANGE);
                    let new_direction = Vec2::new(angle.cos(), angle.sin());
                    *velocity = Velocity(new_direction);
                } else {
                    warn!("[ant_hits_system] There is Some(carrying) but carrying.is_empty - how come?");
                }
            } else {
                commands.entity(ant).add_child(food);
                commands.entity(food).remove::<Collidable>();
                let mut foodpos = match food_query.get_mut(food) {
                    Ok(transform) => transform,
                    Err(e) => {
                        error!("Failed to get food transform: {:?}", e);
                        continue;
                    }
                };
                foodpos.translation = Vec3::new(0.0, 0.0, 0.0);
                commands.entity(ant).insert(Cooldown {
                    distance_squared_remaining: settings.pickup_cooldown_distance_squared(),
                    last_position: ant_transform.translation,
                });

                // Add direction randomization when picking up food
                let current_angle = velocity.0.angle_to(Vec2::new(1.0, 0.0));
                let angle = current_angle
                    + rng.random_range(-Config::TURN_ANGLE_RANGE..Config::TURN_ANGLE_RANGE);
                let new_direction = Vec2::new(angle.cos(), angle.sin());
                *velocity = Velocity(new_direction);
            }
        }
    }
}

pub fn cooldown_system(
    mut query: Query<(Entity, &mut Cooldown, &Transform), With<Ant>>,
    mut commands: Commands,
) {
    for (entity, mut cooldown, transform) in query.iter_mut() {
        // Calculate squared distance moved since last frame (avoids sqrt)
        let distance_squared_moved =
            (transform.translation - cooldown.last_position).length_squared();
        cooldown.distance_squared_remaining -= distance_squared_moved;

        // Update last position for next frame
        cooldown.last_position = transform.translation;

        // Remove cooldown if distance requirement met
        if cooldown.distance_squared_remaining <= 0.0 {
            commands.entity(entity).remove::<Cooldown>();
        }
    }
}

// Frame counter system for tests
pub fn frame_counter_system(mut frame_count: ResMut<FrameCount>) {
    frame_count.0 += 1;
}
