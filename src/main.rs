use rand::Rng;
use derive_more::From;
use bevy::prelude::*;

mod boundary;
use boundary::{BoundaryPlugin, BoundaryWrap, Bounding}; //BoundaryRemoval

fn main() {
    println!("Hello, world!");
    App::new()
        .add_plugins(DefaultPlugins)
        .add_plugins(BoundaryPlugin)
        .add_systems(Startup, setup)
        .add_systems(Update, gatherer_movement)
        .run();
}

#[derive(Debug, Component, From)] //, Default, Deref, DerefMut, From, Resource)]
pub struct Velocity(Vec2);

fn setup(
    mut commands: Commands,
) {
    commands.spawn(Camera2dBundle::default());

    let tile_size = Vec2::new(20.0, 20.0);
    let map_size = Vec2::splat(600.0);

    //let half_x = (map_size.x / 2.0) as i32;
    let full_x = map_size.x as i32;

    // Builds and spawns the sprites
    let mut rng = rand::thread_rng();
    let mut sprites = vec![];

    for x in (-full_x..full_x).step_by(50) {
        let sprite = SpriteBundle {
            sprite: Sprite {
                color: Color::rgb(0.55, 0.7, 0.2),
                custom_size: Some(tile_size),
                ..default()
            },
            transform: Transform::from_translation(Vec3::new(x as f32, 100., 0.)),
            ..default()
        };
        let angle = rng.gen_range(0.0..2.0 * std::f32::consts::PI);
        let velocity = Vec2::new(angle.cos(), angle.sin()) * 1000.0;
        sprites.push((
            sprite, 
            Velocity::from(velocity),
            Bounding::from_radius(2.0),
            BoundaryWrap
        ));
    }
    println!("Sprites to spawn: {}", sprites.len());
    commands.spawn_batch(sprites)
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
