use an_gatherers::*;

use bevy::{
    diagnostic::{FrameTimeDiagnosticsPlugin, LogDiagnosticsPlugin},
    prelude::*,
    window::{PresentMode, PrimaryWindow},
};
use rand::Rng;

fn main() {
    info!("Starting gatherers simulation");
    App::new()
        .insert_resource(ClearColor(Colors::BACKGROUND))
        .insert_resource(SimulationSettings::default())
        .add_plugins(DefaultPlugins.set(WindowPlugin {
            primary_window: Some(Window {
                title: "an-gatherers".to_string(),
                present_mode: PresentMode::AutoVsync,
                ..Default::default()
            }),
            ..Default::default()
        }))
        .add_plugins(BoundaryPlugin)
        .add_plugins(CollisionPlugin::<Food, Ant>::new())
        .add_plugins(UiPlugin)
        .add_plugins(FrameTimeDiagnosticsPlugin::default())
        .add_plugins(LogDiagnosticsPlugin::default())
        .add_systems(Startup, setup)
        .add_systems(Startup, setup_window)
        .add_systems(Update, gatherer_movement)
        .add_systems(Update, ant_hits_system)
        .add_systems(PostUpdate, cooldown_system)
        .run();
}

fn setup(
    mut commands: Commands,
    primary_window: Query<&Window, With<PrimaryWindow>>,
    settings: Res<SimulationSettings>,
) {
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

    let ant_count = spawn_ants(&mut commands, half_x, &settings);
    let food_count = spawn_food(&mut commands, half_x, half_y, &settings);

    info!("Spawned {} ants and {} food items", ant_count, food_count);
}

fn spawn_ants(commands: &mut Commands, half_x: i32, settings: &SimulationSettings) -> usize {
    let mut rng = rand::rng();
    let mut count = 0;

    for x in (-half_x..half_x).step_by(Config::ANT_SPAWN_STEP as usize) {
        let angle = rng.random_range(0.0..2.0 * std::f32::consts::PI);
        let direction = Vec2::new(angle.cos(), angle.sin());

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
            Velocity::from(direction),
            Bounding::from_radius(settings.collision_radius()),
            Collidable,
            BoundaryWrap,
        ));
        count += 1;
    }
    count
}

fn spawn_food(
    commands: &mut Commands,
    half_x: i32,
    half_y: i32,
    settings: &SimulationSettings,
) -> usize {
    let mut rng = rand::rng();

    for _ in 0..Config::FOOD_COUNT {
        let x = rng.random_range(-half_x..half_x);
        let y = rng.random_range(-half_y..half_y);

        commands.spawn((
            Food,
            Sprite {
                color: Colors::FOOD,
                custom_size: Some(Config::FOOD_SIZE),
                ..default()
            },
            Transform::from_translation(Vec3::new(x as f32, y as f32, Config::FOOD_Z_LAYER)),
            Collidable,
            Bounding::from_radius(settings.collision_radius()),
        ));
    }
    Config::FOOD_COUNT as usize
}

fn setup_window(mut windows: Query<&mut Window>) {
    if let Ok(mut window) = windows.single_mut() {
        window.resizable = true;
        window.fit_canvas_to_parent = true;
    }
}
