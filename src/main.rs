mod boundary;
mod collision;
mod spatial_index;

use bevy::{
    diagnostic::{FrameTimeDiagnosticsPlugin, LogDiagnosticsPlugin},
    log,
    prelude::*,
    window::{PresentMode, PrimaryWindow},
};
use derive_more::From;
use rand::Rng;

use boundary::{BoundaryPlugin, BoundaryWrap, Bounding}; //BoundaryRemoval
use collision::{Collidable, CollisionPlugin, HitEvent}; //CollisionSystemLabel

fn main() {
    println!("Hello, world!");
    App::new()
        .insert_resource(ClearColor(Color::srgb(
            95. / 255.,
            151. / 255.,
            212. / 255.,
        )))
        .add_plugins(DefaultPlugins.set(WindowPlugin {
            primary_window: Some(Window {
                title: "an-gatherers".to_string(),
                //resolution: (800.0, 600.0).into(),
                present_mode: PresentMode::Immediate, //PresentMode::AutoVsync,
                ..Default::default()
            }),
            ..Default::default()
        }))
        .add_plugins(BoundaryPlugin)
        .add_plugins(CollisionPlugin::<Food, Ant>::new())
        .add_plugins(FrameTimeDiagnosticsPlugin)
        .add_plugins(LogDiagnosticsPlugin::default())
        .add_systems(Startup, setup)
        .add_systems(Update, gatherer_movement)
        .add_systems(Update, ant_hits_system)
        .add_systems(PostUpdate, cooldown_system)
        .run();
}

#[derive(Debug, Component, From)] //, Default, Deref, DerefMut, From, Resource)]
pub struct Velocity(Vec2);

#[derive(Debug, Component, Default)]
struct Ant;

#[derive(Debug, Component, Default)]
struct Food;

#[derive(Debug, Component)]
struct Cooldown {
    timer: Timer,
}

fn setup(mut commands: Commands, primary_window: Query<&Window, With<PrimaryWindow>>) {
    commands.spawn(Camera2dBundle::default());
    let window = match primary_window.get_single() {
        Ok(window) => window,
        Err(e) => {
            log::error!("Failed to get primary window: {:?}", e);
            return;
        }
    };

    let map_size = Vec2::new(window.width(), window.height()); //splat(600.0);
    let ant_size = Vec2::new(20.0, 20.0);
    let food_size = Vec2::new(10.0, 10.0);

    let half_x = (map_size.x / 2.0) as i32;
    let half_y = (map_size.y / 2.0) as i32;

    // Builds and spawns the sprites
    let mut rng = rand::thread_rng();
    let mut ants = vec![];

    for x in (-half_x..half_x).step_by(50) {
        let ant = SpriteBundle {
            sprite: Sprite {
                color: Color::srgb(0.8, 0.8, 0.8),
                custom_size: Some(ant_size),
                ..default()
            },
            transform: Transform::from_translation(Vec3::new(x as f32, 100., 2.)),
            ..default()
        };
        let angle = rng.gen_range(0.0..2.0 * std::f32::consts::PI);
        let velocity = Vec2::new(angle.cos(), angle.sin()) * 1000.0;
        ants.push((
            Ant,
            ant,
            Velocity::from(velocity),
            Bounding::from_radius(10.0),
            Collidable,
            BoundaryWrap,
        ));
    }

    let mut foods = vec![];
    for _ in 0..80 {
        let x = rng.gen_range(-half_x..half_x);
        let y = rng.gen_range(-half_y..half_y);

        let food = SpriteBundle {
            sprite: Sprite {
                color: Color::srgb(192. / 255., 2. / 255., 2. / 255.),
                custom_size: Some(food_size),
                ..default()
            },
            transform: Transform::from_translation(Vec3::new(x as f32, y as f32, 1.)),
            ..default()
        };
        foods.push((Food, food, Collidable, Bounding::from_radius(10.0)));
    }

    println!("Ants to spawn: {}", ants.len());
    commands.spawn_batch(ants);
    commands.spawn_batch(foods);
}

/// The sprite is animated by changing its translation depending on the time that has passed since
/// the last frame.
fn gatherer_movement(time: Res<Time>, mut sprite_position: Query<(&Velocity, &mut Transform)>) {
    //let sprite_position_collection: Vec<_> = sprite_position.iter_mut().collect();
    //print!("[gatherer_movement] Number of sprites: {}", sprite_position_collection.len());
    for (velocity, mut transform) in &mut sprite_position {
        let scaled_velocity = velocity.0 * time.delta_seconds();
        transform.translation.x += scaled_velocity.x;
        transform.translation.y += scaled_velocity.y;
    }
}

fn ant_hits_system(
    //mut rng: Local<Random>,
    mut ant_hits: EventReader<HitEvent<Food, Ant>>,
    mut commands: Commands,
    mut ant_query: Query<(&mut Velocity, Option<&Children>), (With<Ant>, Without<Cooldown>)>,
    mut food_query: Query<&mut Transform, With<Food>>,
) {
    let mut rng = rand::thread_rng();

    for hit in ant_hits.read() {
        let ant = hit.hitter();
        let food = hit.hittable();

        //println!("Hit 1: {}", ant.index());
        if let Ok((mut velocity, carrying)) = ant_query.get_mut(ant) {
            //println!("[ant_hits_system] Hit: {}", ant.index());
            if let Some(carrying) = carrying {
                if !carrying.is_empty() {
                    let carried_food = carrying[0];
                    commands.entity(carried_food).remove_parent_in_place();
                    commands.entity(carried_food).insert(Collidable); //'Carried' component not needed, Collidable is same but negative
                                                                      //println!("[ant_hits_system] Dropped: {}", carried_food.index());
                    commands.entity(ant).insert(Cooldown {
                        timer: Timer::from_seconds(0.1, TimerMode::Once),
                    });
                } else {
                    log::warn!("[ant_hits_system] There is Some(carrying) but carrying.is_empty - how come?")
                }
            } else {
                commands.entity(ant).push_children(&[food]);
                commands.entity(food).remove::<Collidable>(); //'Carried' component not needed, Collidable is same but negative

                let mut foodpos = match food_query.get_mut(food) {
                    Ok(transform) => transform,
                    Err(e) => {
                        log::error!("Failed to get food transform: {:?}", e);
                        continue;
                    }
                };
                foodpos.translation.x = 0.0;
                foodpos.translation.y = 0.0;
                foodpos.translation.z = 3.0; //over the ant
                                             //println!("[ant_hits_system] Picked up: {}", food.index());

                // Get the current direction of the ant
                let current_angle = velocity.0.angle_between(Vec2::new(1.0, 0.0));

                // Turn back 180 degrees and add a random angle from -90 to 90 degrees
                let angle = current_angle
                    + std::f32::consts::PI
                    + rng.gen_range(-std::f32::consts::FRAC_PI_2..std::f32::consts::FRAC_PI_2);

                let new_velocity = Vec2::new(angle.cos(), angle.sin()) * 1000.0;
                *velocity = Velocity(new_velocity);
            }
        }
    }
}

fn cooldown_system(
    time: Res<Time>,
    mut query: Query<(Entity, &mut Cooldown)>,
    mut commands: Commands,
) {
    for (entity, mut cooldown) in query.iter_mut() {
        cooldown.timer.tick(time.delta());
        if cooldown.timer.finished() {
            commands.entity(entity).remove::<Cooldown>();
        }
    }
}
