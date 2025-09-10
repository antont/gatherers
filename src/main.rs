mod boundary;
mod collision;
mod config;
mod spatial_index;

use bevy::{
    diagnostic::{FrameTimeDiagnosticsPlugin, LogDiagnosticsPlugin},
    prelude::*,
    window::{PresentMode, PrimaryWindow},
};
use derive_more::From;

use rand::Rng;

use boundary::{BoundaryPlugin, BoundaryWrap, Bounding}; //BoundaryRemoval
use collision::{Collidable, CollisionPlugin, HitEvent}; //CollisionSystemLabel
use config::{Colors, Config};

fn main() {
    info!("Starting gatherers simulation");
    App::new()
        .insert_resource(ClearColor(Colors::BACKGROUND))
        .add_plugins(DefaultPlugins.set(WindowPlugin {
            primary_window: Some(Window {
                title: "an-gatherers".to_string(),
                //resolution: (800.0, 600.0).into(),
                present_mode: PresentMode::AutoVsync, //PresentMode::Immediate,
                ..Default::default()
            }),
            ..Default::default()
        }))
        .add_plugins(BoundaryPlugin)
        .add_plugins(CollisionPlugin::<Food, Ant>::new())
        .add_plugins(FrameTimeDiagnosticsPlugin::default())
        .add_plugins(LogDiagnosticsPlugin::default())
        .add_systems(Startup, setup)
        .add_systems(Startup, setup_window)
        .add_systems(Update, gatherer_movement)
        .add_systems(Update, ant_hits_system)
        .add_systems(PostUpdate, cooldown_system)
        .add_systems(Update, handle_window_resize)
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
    commands.spawn(Camera2d::default());
    let window = match primary_window.single() {
        Ok(window) => window,
        Err(e) => {
            error!("Failed to get primary window: {:?}", e);
            return;
        }
    };

    let map_size = Vec2::new(window.width(), window.height());
    let half_x = (map_size.x / 2.0) as i32;
    let half_y = (map_size.y / 2.0) as i32;

    let ant_count = spawn_ants(&mut commands, half_x);
    let food_count = spawn_food(&mut commands, half_x, half_y);

    info!("Spawned {} ants and {} food items", ant_count, food_count);
}

fn spawn_ants(commands: &mut Commands, half_x: i32) -> usize {
    let mut rng = rand::thread_rng();
    let mut count = 0;

    for x in (-half_x..half_x).step_by(Config::ANT_SPAWN_STEP as usize) {
        let angle = rng.gen_range(0.0..2.0 * std::f32::consts::PI);
        let velocity = Vec2::new(angle.cos(), angle.sin()) * Config::ANT_SPEED;

        commands.spawn((
            Ant,
            Sprite {
                color: Colors::ANT,
                custom_size: Some(Config::ANT_SIZE),
                ..default()
            },
            Transform::from_translation(Vec3::new(
                x as f32,
                Config::ANT_SPAWN_Y,
                Config::ANT_Z_LAYER,
            )),
            GlobalTransform::default(),
            Velocity::from(velocity),
            Bounding::from_radius(Config::COLLISION_RADIUS),
            Collidable,
            BoundaryWrap,
            Visibility::default(),
        ));
        count += 1;
    }
    count
}

fn spawn_food(commands: &mut Commands, half_x: i32, half_y: i32) -> usize {
    let mut rng = rand::thread_rng();

    for _ in 0..Config::FOOD_COUNT {
        let x = rng.gen_range(-half_x..half_x);
        let y = rng.gen_range(-half_y..half_y);

        commands.spawn((
            Food,
            Sprite {
                color: Colors::FOOD,
                custom_size: Some(Config::FOOD_SIZE),
                ..default()
            },
            Transform::from_translation(Vec3::new(x as f32, y as f32, Config::FOOD_Z_LAYER)),
            GlobalTransform::default(),
            Collidable,
            Bounding::from_radius(Config::COLLISION_RADIUS),
            Visibility::default(),
        ));
    }
    Config::FOOD_COUNT as usize
}

/// The sprite is animated by changing its translation depending on the time that has passed since
/// the last frame.
fn gatherer_movement(time: Res<Time>, mut sprite_position: Query<(&Velocity, &mut Transform)>) {
    //let sprite_position_collection: Vec<_> = sprite_position.iter_mut().collect();
    //print!("[gatherer_movement] Number of sprites: {}", sprite_position_collection.len());
    for (velocity, mut transform) in &mut sprite_position {
        let scaled_velocity = velocity.0 * time.delta_secs();
        transform.translation.x += scaled_velocity.x;
        transform.translation.y += scaled_velocity.y;
    }
}

fn ant_hits_system(
    mut ant_hits: EventReader<HitEvent<Food, Ant>>,
    mut commands: Commands,
    mut ant_query: Query<(&mut Velocity, Option<&Children>), (With<Ant>, Without<Cooldown>)>,
    mut food_query: Query<&mut Transform, With<Food>>,
) {
    let mut rng = rand::thread_rng();

    for hit in ant_hits.read() {
        let ant = hit.hitter();
        let food = hit.hittable();

        if let Ok((mut velocity, carrying)) = ant_query.get_mut(ant) {
            if let Some(carrying) = carrying {
                if !carrying.is_empty() {
                    let carried_food = carrying[0];
                    commands.entity(carried_food).remove_parent_in_place();
                    commands.entity(carried_food).insert(Collidable);
                    commands.entity(ant).insert(Cooldown {
                        timer: Timer::from_seconds(Config::PICKUP_COOLDOWN, TimerMode::Once),
                    });
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

                // Get the current direction of the ant
                let current_angle = velocity.0.angle_to(Vec2::new(1.0, 0.0));

                // Turn back 180 degrees and add a random angle within the configured range
                let angle = current_angle
                    + std::f32::consts::PI
                    + rng.gen_range(-Config::TURN_ANGLE_RANGE..Config::TURN_ANGLE_RANGE);

                let new_velocity = Vec2::new(angle.cos(), angle.sin()) * Config::ANT_SPEED;
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

fn setup_window(mut windows: Query<&mut Window>) {
    if let Ok(mut window) = windows.single_mut() {
        window.resizable = true;
        // Make the window fill the available space
        window.fit_canvas_to_parent = true;
    }
}

fn handle_window_resize(mut _windows: Query<&mut Window>) {
    // Optional - add custom resize logic here if needed
}
