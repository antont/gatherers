pub mod boundary;
pub mod collision;
pub mod config;
pub mod spatial_index;
pub mod ui;

use bevy::prelude::*;
use derive_more::From;
use rand::Rng;

pub use boundary::{BoundaryPlugin, BoundaryWrap, Bounding};
pub use collision::{Collidable, CollisionPlugin, HitEvent};
pub use config::{Colors, Config, SimulationSettings};
pub use spatial_index::SpatialIndex;
pub use ui::UiPlugin;

#[derive(Resource, Default)]
pub struct FrameCount(pub u64);

#[derive(Debug, Component, From)]
pub struct Velocity(pub Vec2);

#[derive(Debug, Component, Default)]
pub struct Ant;

#[derive(Debug, Component, Default)]
pub struct Food;

#[derive(Debug, Component)]
pub struct Cooldown {
    pub timer: f32,
}

pub fn gatherer_movement(
    time: Res<Time>,
    settings: Res<SimulationSettings>,
    mut sprite_position: Query<(&Velocity, &mut Transform), With<Ant>>,
) {
    let max_movement = settings.max_movement_per_frame();
    let delta_time = time.delta_secs();

    let current_speed = settings.ant_speed();

    for (velocity, mut transform) in &mut sprite_position {
        let direction = velocity.0.normalize_or_zero();
        let actual_velocity = direction * current_speed;
        let desired_movement = actual_velocity * delta_time;

        let movement_distance = desired_movement.length();
        let final_movement = if movement_distance > max_movement {
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
) {
    let mut rng = rand::rng();

    for hit in ant_hits.read() {
        let ant = hit.hitter();
        let food = hit.hittable();

        if let Ok((mut velocity, carrying, ant_transform)) = ant_query.get_mut(ant) {
            if let Some(carrying) = carrying {
                if !carrying.is_empty() {
                    let carried_food = carrying[0];

                    // Set food to the ant's world position before detaching,
                    // because remove::<ChildOf>() keeps the local Transform as-is
                    // and it would otherwise snap to origin (the old local offset).
                    if let Ok(mut food_transform) = food_query.get_mut(carried_food) {
                        food_transform.translation = Vec3::new(
                            ant_transform.translation.x,
                            ant_transform.translation.y,
                            Config::FOOD_Z_LAYER,
                        );
                    }

                    commands.entity(carried_food).remove::<ChildOf>();
                    commands.entity(carried_food).insert(Collidable);
                    commands.entity(ant).insert(Cooldown {
                        timer: Config::BASE_PICKUP_COOLDOWN,
                    });

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
                foodpos.translation.x = 0.0;
                foodpos.translation.y = 0.0;
                foodpos.translation.z = Config::CARRIED_FOOD_Z_LAYER;

                let current_angle = velocity.0.angle_to(Vec2::new(1.0, 0.0));
                let angle = current_angle
                    + std::f32::consts::PI
                    + rng.random_range(-Config::TURN_ANGLE_RANGE..Config::TURN_ANGLE_RANGE);
                let new_direction = Vec2::new(angle.cos(), angle.sin());
                *velocity = Velocity(new_direction);
            }
        }
    }
}

pub fn cooldown_system(
    time: Res<Time>,
    mut query: Query<(Entity, &mut Cooldown), With<Ant>>,
    mut commands: Commands,
) {
    for (entity, mut cooldown) in query.iter_mut() {
        cooldown.timer -= time.delta_secs();
        if cooldown.timer <= 0.0 {
            commands.entity(entity).remove::<Cooldown>();
        }
    }
}

pub fn frame_counter_system(mut frame_count: ResMut<FrameCount>) {
    frame_count.0 += 1;
}

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

        let cell_x = (pos.x / 20.0).floor() as i32;
        let cell_y = (pos.y / 20.0).floor() as i32;
        println!("    Hitter in spatial cell ({}, {})", cell_x, cell_y);

        let nearby_entities = spatial_index.get_nearby(pos);
        println!(
            "    Nearby entities from spatial index: {} entities",
            nearby_entities.len()
        );

        for &nearby_entity in nearby_entities.iter() {
            println!("      Checking nearby entity {:?}", nearby_entity);

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
