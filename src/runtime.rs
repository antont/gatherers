use std::collections::HashMap;

use bevy::prelude::*;
use rand::{Rng, SeedableRng, rngs::StdRng};

use crate::{Config, SimulationSettings};

#[derive(Resource, Clone, Debug, PartialEq)]
pub struct RuntimeConfig {
    pub backend_ws_url: Option<String>,
    pub sim_id: String,
    pub seed: Option<u64>,
    pub startup_speed: f32,
    pub window_title: String,
    pub window_position: Option<(i32, i32)>,
    pub window_size: Option<(u32, u32)>,
}

impl Default for RuntimeConfig {
    fn default() -> Self {
        Self {
            backend_ws_url: std::env::var("GATHERERS_BACKEND_WS_URL").ok(),
            sim_id: format!("sim-{}", rand::random::<u64>()),
            seed: None,
            startup_speed: SimulationSettings::default().speed_multiplier,
            window_title: "an-gatherers".to_string(),
            window_position: None,
            window_size: None,
        }
    }
}

impl RuntimeConfig {
    pub fn from_env() -> Result<Self, String> {
        Self::from_pairs(std::env::vars())
    }

    pub fn from_pairs<I, K, V>(pairs: I) -> Result<Self, String>
    where
        I: IntoIterator<Item = (K, V)>,
        K: Into<String>,
        V: Into<String>,
    {
        let values: HashMap<String, String> = pairs
            .into_iter()
            .map(|(key, value)| (key.into(), value.into()))
            .collect();

        let mut config = Self::default();
        if let Some(url) = values.get("GATHERERS_BACKEND_WS_URL") {
            config.backend_ws_url = Some(url.clone());
        }
        if let Some(sim_id) = values.get("GATHERERS_SIM_ID") {
            config.sim_id = sim_id.clone();
        }
        if let Some(seed) = values.get("GATHERERS_SIM_SEED") {
            config.seed = Some(parse_u64("GATHERERS_SIM_SEED", seed)?);
        }
        if let Some(speed) = values.get("GATHERERS_STARTUP_SPEED") {
            config.startup_speed = parse_f32("GATHERERS_STARTUP_SPEED", speed)?;
        }
        if let Some(title) = values.get("GATHERERS_WINDOW_TITLE") {
            config.window_title = title.clone();
        }

        let window_x = values
            .get("GATHERERS_WINDOW_X")
            .map(|value| parse_i32("GATHERERS_WINDOW_X", value))
            .transpose()?;
        let window_y = values
            .get("GATHERERS_WINDOW_Y")
            .map(|value| parse_i32("GATHERERS_WINDOW_Y", value))
            .transpose()?;
        config.window_position = match (window_x, window_y) {
            (Some(x), Some(y)) => Some((x, y)),
            (None, None) => None,
            _ => {
                return Err(
                    "GATHERERS_WINDOW_X and GATHERERS_WINDOW_Y must be provided together"
                        .to_string(),
                );
            }
        };

        let window_width = values
            .get("GATHERERS_WINDOW_WIDTH")
            .map(|value| parse_u32("GATHERERS_WINDOW_WIDTH", value))
            .transpose()?;
        let window_height = values
            .get("GATHERERS_WINDOW_HEIGHT")
            .map(|value| parse_u32("GATHERERS_WINDOW_HEIGHT", value))
            .transpose()?;
        config.window_size = match (window_width, window_height) {
            (Some(width), Some(height)) => Some((width, height)),
            (None, None) => None,
            _ => {
                return Err(
                    "GATHERERS_WINDOW_WIDTH and GATHERERS_WINDOW_HEIGHT must be provided together"
                        .to_string(),
                );
            }
        };

        Ok(config)
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct SpawnLayout {
    pub ants: Vec<AntSpawn>,
    pub food_positions: Vec<Vec2>,
}

#[derive(Clone, Debug, PartialEq)]
pub struct AntSpawn {
    pub position: Vec3,
    pub direction: Vec2,
}

pub fn generate_spawn_layout(
    map_size: Vec2,
    _settings: &SimulationSettings,
    seed: Option<u64>,
) -> SpawnLayout {
    let half_x = (map_size.x / 2.0) as i32;
    let half_y = (map_size.y / 2.0) as i32;
    let mut rng = build_rng(seed);

    let ants = (-half_x..half_x)
        .step_by(Config::ANT_SPAWN_STEP as usize)
        .map(|x| {
            let angle = rng.random_range(0.0..2.0 * std::f32::consts::PI);
            let direction = Vec2::new(angle.cos(), angle.sin());
            AntSpawn {
                position: Vec3::new(x as f32, Config::ANT_SPAWN_Y, Config::ANT_Z_LAYER),
                direction,
            }
        })
        .collect();

    let food_positions = (0..Config::FOOD_COUNT)
        .map(|_| {
            let x = rng.random_range(-half_x..half_x);
            let y = rng.random_range(-half_y..half_y);
            Vec2::new(x as f32, y as f32)
        })
        .collect();

    SpawnLayout {
        ants,
        food_positions,
    }
}

fn build_rng(seed: Option<u64>) -> StdRng {
    match seed {
        Some(seed) => StdRng::seed_from_u64(seed),
        None => StdRng::from_os_rng(),
    }
}

fn parse_u64(name: &str, value: &str) -> Result<u64, String> {
    value
        .parse()
        .map_err(|_| format!("{name} must be a valid u64, got {value:?}"))
}

fn parse_u32(name: &str, value: &str) -> Result<u32, String> {
    value
        .parse()
        .map_err(|_| format!("{name} must be a valid u32, got {value:?}"))
}

fn parse_i32(name: &str, value: &str) -> Result<i32, String> {
    value
        .parse()
        .map_err(|_| format!("{name} must be a valid i32, got {value:?}"))
}

fn parse_f32(name: &str, value: &str) -> Result<f32, String> {
    value
        .parse()
        .map_err(|_| format!("{name} must be a valid f32, got {value:?}"))
}
