use bevy::prelude::*;

fn main() {
    println!("Hello, world!");
    App::new()
        .add_plugins(DefaultPlugins)
        .add_systems(Startup, setup)
        .add_systems(Update, sprite_movement)
        .run();
}

#[derive(Component, Clone)]
enum Direction {
    Up,
    Down,
}

fn setup(
    mut commands: Commands,
) {
    commands.spawn(Camera2dBundle::default());

    let tile_size = Vec2::new(20.0, 20.0);
    let map_size = Vec2::splat(600.0);

    //let half_x = (map_size.x / 2.0) as i32;
    let full_x = map_size.x as i32;

    // Builds and spawns the sprites
    let mut sprites = vec![];

    let mut next_dir = Direction::Down;
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
        sprites.push((sprite, next_dir.clone()));
        match next_dir {
            Direction::Down => next_dir = Direction::Up,
            Direction::Up => next_dir = Direction::Down,
        }
    }
    println!("Sprites to spawn: {}", sprites.len());
    commands.spawn_batch(sprites)
}

/// The sprite is animated by changing its translation depending on the time that has passed since
/// the last frame.
fn sprite_movement(time: Res<Time>, mut sprite_position: Query<(&mut Direction, &mut Transform)>) {
    for (mut sprite, mut transform) in &mut sprite_position {
        match *sprite {
            Direction::Up => transform.translation.y += 150. * time.delta_seconds(),
            Direction::Down => transform.translation.y -= 150. * time.delta_seconds(),
        }

        if transform.translation.y > 200. {
            *sprite = Direction::Down;
        } else if transform.translation.y < -200. {
            *sprite = Direction::Up;
        }
    }
}
