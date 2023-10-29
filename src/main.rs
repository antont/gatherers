use rand::Rng;
use derive_more::From;
use bevy::{prelude::*, window::PrimaryWindow, utils::HashSet};

mod boundary;
mod collision;
use boundary::{BoundaryPlugin, BoundaryWrap, Bounding}; //BoundaryRemoval
use collision::{Collidable, CollisionPlugin, CollisionSystemLabel, HitEvent};

fn main() {
    println!("Hello, world!");
    App::new()
        .add_plugins(DefaultPlugins)
        .add_plugins(BoundaryPlugin)
        .add_plugins(CollisionPlugin::<Food, Ant>::new())
        .add_systems(Startup, setup)
        .add_systems(Update, gatherer_movement)
        .add_systems(Update, ant_hits_system)        
        .run();
}

#[derive(Debug, Component, From)] //, Default, Deref, DerefMut, From, Resource)]
pub struct Velocity(Vec2);

#[derive(Debug, Component, Default)]
struct Ant;

#[derive(Debug, Component, Default)]
struct Food;


fn setup(
    mut commands: Commands,
    primary_window: Query<&Window, With<PrimaryWindow>>
) {
    commands.spawn(Camera2dBundle::default());
    let window = primary_window.get_single().unwrap();

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
                color: Color::rgb(0.55, 0.7, 0.2),
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
            BoundaryWrap
        ));
    }

    let mut foods = vec![];
    for _ in 0..30 {
        let x = rng.gen_range(-half_x..half_x);
        let y = rng.gen_range(-half_y..half_y);

        let food = SpriteBundle {
            sprite: Sprite {
                color: Color::rgb(0.8, 0.4, 0.3),
                custom_size: Some(food_size),
                ..default()
            },
            transform: Transform::from_translation(Vec3::new(x as f32, y as f32, 1.)),
            ..default()
        };
        foods.push((
            Food,
            food,
            Collidable,
            Bounding::from_radius(5.0),
        ));
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
    query: Query<&Transform, With<Ant>>
) {
    let mut remove = HashSet::with_capacity(ant_hits.len());

    for hit in ant_hits.iter() {
        let ant = hit.hitter();
        let food = hit.hittable();

        /* somehow does not help?
        if remove.contains(&food) {
            continue;
        }*/

        //println!("Hit 1: {}", ant.index());
        if let Ok(transform) = query.get(ant) {
            println!("Hit 2: {}", ant.index());
            if !remove.contains(&food) {
                remove.insert(food);
            }
        }
            /*for n in 0..12 * 6 {
                let angle = 2.0 * PI / 12.0 * (n % 12) as f32 + rng.gen_range(0.0..2.0 * PI / 12.0);
                let direction = Vec3::new(angle.cos(), angle.sin(), 0.0);
                let position = direction * rng.gen_range(1.0..20.0) + transform.translation;
            }*/
    }

    for food in remove {
        commands.entity(food).despawn();
    }
}

