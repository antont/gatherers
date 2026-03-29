use an_gatherers::*;

use bevy::{
    diagnostic::{FrameTimeDiagnosticsPlugin, LogDiagnosticsPlugin},
    prelude::*,
    window::{PresentMode, PrimaryWindow, WindowPosition},
};
use log::{error, info};

fn main() {
    info!("Starting gatherers simulation");
    let runtime = RuntimeConfig::from_env().expect("runtime config should parse");
    App::new()
        .insert_resource(ClearColor(Colors::BACKGROUND))
        .insert_resource(SimulationSettings {
            speed_multiplier: runtime.startup_speed,
        })
        .insert_resource(BackendClientConfig {
            url: runtime.backend_ws_url.clone(),
            sim_id: runtime.sim_id.clone(),
        })
        .insert_resource(runtime)
        .add_plugins(DefaultPlugins.set(WindowPlugin {
            primary_window: Some(Window {
                present_mode: PresentMode::AutoVsync,
                ..Default::default()
            }),
            ..Default::default()
        }))
        .add_plugins(BoundaryPlugin)
        .add_plugins(CollisionPlugin::<Food, Ant>::new())
        .add_plugins(BackendClientPlugin)
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
    runtime: Res<RuntimeConfig>,
    settings: Res<SimulationSettings>,
) {
    let runtime = runtime.as_ref();
    commands.spawn(Camera2d::default());
    let window = match primary_window.single() {
        Ok(window) => window,
        Err(e) => {
            error!("Failed to get primary window: {:?}", e);
            return;
        }
    };

    let map_size = Vec2::new(window.width(), window.height());
    let layout = generate_spawn_layout(map_size, &settings, runtime.seed);
    let ant_count = spawn_ants(&mut commands, &settings, &layout);
    let food_count = spawn_food(&mut commands, &settings, &layout);

    info!("Spawned {} ants and {} food items", ant_count, food_count);
}

fn spawn_ants(
    commands: &mut Commands,
    settings: &SimulationSettings,
    layout: &an_gatherers::runtime::SpawnLayout,
) -> usize {
    for ant in &layout.ants {
        commands.spawn((
            Ant,
            Sprite {
                color: Colors::ANT,
                custom_size: Some(Config::ANT_SIZE),
                ..default()
            },
            Transform::from_translation(ant.position),
            Velocity::from(ant.direction),
            Bounding::from_radius(settings.collision_radius()),
            Collidable,
            BoundaryWrap,
        ));
    }
    layout.ants.len()
}

fn spawn_food(
    commands: &mut Commands,
    settings: &SimulationSettings,
    layout: &an_gatherers::runtime::SpawnLayout,
) -> usize {
    for position in &layout.food_positions {
        commands.spawn((
            Food,
            Sprite {
                color: Colors::FOOD,
                custom_size: Some(Config::FOOD_SIZE),
                ..default()
            },
            Transform::from_translation(Vec3::new(position.x, position.y, Config::FOOD_Z_LAYER)),
            Collidable,
            Bounding::from_radius(settings.collision_radius()),
        ));
    }
    layout.food_positions.len()
}

fn setup_window(mut windows: Query<&mut Window>, runtime: Res<RuntimeConfig>) {
    let runtime = runtime.as_ref();
    if let Ok(mut window) = windows.single_mut() {
        window.title = runtime.window_title.clone();
        if let Some((x, y)) = runtime.window_position {
            window.position = WindowPosition::new(IVec2::new(x, y));
        }
        if let Some((width, height)) = runtime.window_size {
            window.resolution.set(width as f32, height as f32);
        }
        window.resizable = true;
        window.fit_canvas_to_parent = true;
    }
}
