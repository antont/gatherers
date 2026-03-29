use an_gatherers::{RuntimeConfig, SimulationSettings, generate_spawn_layout};
use bevy::prelude::Vec2;

#[test]
fn parses_explicit_runtime_env_values() {
    let config = RuntimeConfig::from_pairs([
        ("GATHERERS_BACKEND_WS_URL", "ws://127.0.0.1:18080/ws/ingest"),
        ("GATHERERS_SIM_ID", "demo-sim-03"),
        ("GATHERERS_SIM_SEED", "123"),
        ("GATHERERS_STARTUP_SPEED", "9.5"),
        ("GATHERERS_WINDOW_TITLE", "Demo Sim 03"),
        ("GATHERERS_WINDOW_X", "640"),
        ("GATHERERS_WINDOW_Y", "360"),
        ("GATHERERS_WINDOW_WIDTH", "800"),
        ("GATHERERS_WINDOW_HEIGHT", "600"),
    ])
    .expect("runtime config should parse");

    assert_eq!(
        config.backend_ws_url.as_deref(),
        Some("ws://127.0.0.1:18080/ws/ingest")
    );
    assert_eq!(config.sim_id, "demo-sim-03");
    assert_eq!(config.seed, Some(123));
    assert_eq!(config.startup_speed, 9.5);
    assert_eq!(config.window_title, "Demo Sim 03");
    assert_eq!(config.window_position, Some((640, 360)));
    assert_eq!(config.window_size, Some((800, 600)));
}

#[test]
fn same_seed_produces_same_spawn_layout() {
    let settings = SimulationSettings::default();
    let map_size = Vec2::new(1280.0, 720.0);

    let left = generate_spawn_layout(map_size, &settings, Some(42));
    let right = generate_spawn_layout(map_size, &settings, Some(42));

    assert_eq!(
        left, right,
        "same configured seed should reproduce the same initial sim layout"
    );
}

#[test]
fn different_seeds_change_spawn_layout() {
    let settings = SimulationSettings::default();
    let map_size = Vec2::new(1280.0, 720.0);

    let left = generate_spawn_layout(map_size, &settings, Some(1));
    let right = generate_spawn_layout(map_size, &settings, Some(2));

    assert_ne!(
        left, right,
        "different configured seeds should not produce the same initial sim layout"
    );
}
