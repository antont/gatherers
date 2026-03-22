//! Configuration constants and runtime settings for the gatherers simulation.
//! Contains all configurable parameters in one place to avoid magic numbers throughout the codebase.

use bevy::prelude::*;

/// Static configuration constants for the simulation.
/// These values don't change at runtime for optimal performance.
pub struct Config;

impl Config {
    /// Size of ant sprites
    pub const ANT_SIZE: Vec2 = Vec2::new(20.0, 20.0);

    /// Size of food sprites
    pub const FOOD_SIZE: Vec2 = Vec2::new(10.0, 10.0);

    /// Step size for ant spawning grid (controls ant density)
    pub const ANT_SPAWN_STEP: i32 = 50;

    /// Y position where ants are spawned
    pub const ANT_SPAWN_Y: f32 = 100.0;

    /// Number of food items to spawn
    pub const FOOD_COUNT: i32 = 80;

    /// Maximum random angle range when turning (in radians, ±90 degrees)
    pub const TURN_ANGLE_RANGE: f32 = std::f32::consts::FRAC_PI_2;

    /// Cell size for spatial indexing collision detection
    /// Should be larger than collision radius for optimal performance
    pub const SPATIAL_CELL_SIZE: f32 = 20.0;

    /// Z-layer for ants
    pub const ANT_Z_LAYER: f32 = 2.0;

    /// Z-layer for food on ground
    pub const FOOD_Z_LAYER: f32 = 1.0;

    /// Z-layer for carried food (should be above ants)
    pub const CARRIED_FOOD_Z_LAYER: f32 = 3.0;

    /// Base speed multiplier for ant movement (used as default)
    pub const BASE_ANT_SPEED: f32 = 100.0;

    /// Base cooldown time after picking up/dropping food (in seconds)
    /// Calibrated for BASE_ANT_SPEED to ensure ants move away from dropped food
    pub const BASE_PICKUP_COOLDOWN: f32 = 1.0;

    /// Base radius for bounding collision detection
    pub const BASE_COLLISION_RADIUS: f32 = 10.0;

    /// Speed multiplier range for UI control
    pub const MIN_SPEED_MULTIPLIER: f32 = 0.1;
    pub const MAX_SPEED_MULTIPLIER: f32 = 10.0;

    /// Special unlimited speed value (negative indicates unlimited)
    pub const UNLIMITED_SPEED: f32 = -1.0;
}

/// Runtime configuration resource that can be modified during gameplay
#[derive(Resource)]
pub struct SimulationSettings {
    /// Current speed multiplier (1.0 = base speed, 10.0 = max speed)
    pub speed_multiplier: f32,
}

impl Default for SimulationSettings {
    fn default() -> Self {
        Self {
            speed_multiplier: 5.0,
        }
    }
}

impl SimulationSettings {
    /// Nominal ant speed in units/second before anti-tunneling capping.
    pub fn ant_speed(&self) -> f32 {
        if self.is_unlimited_speed() {
            Config::BASE_ANT_SPEED * Config::MAX_SPEED_MULTIPLIER * 20.0
        } else {
            Config::BASE_ANT_SPEED * self.speed_multiplier
        }
    }

    pub fn is_unlimited_speed(&self) -> bool {
        self.speed_multiplier == Config::UNLIMITED_SPEED
    }

    pub fn collision_radius(&self) -> f32 {
        Config::BASE_COLLISION_RADIUS
    }

    /// Maximum safe displacement per frame to prevent tunneling.
    /// Derived from collision geometry: must be smaller than the sum of
    /// ant + food radii so the discrete overlap check never misses.
    pub fn safe_step_distance(&self) -> f32 {
        self.collision_radius() * 2.0 * 0.9
    }

    /// Effective speed in units/sec after accounting for the anti-tunneling cap.
    /// For unlimited mode this is the fastest collision-safe speed for the
    /// current frame duration; for normal modes it equals the nominal speed
    /// unless the frame is so slow it would cause tunneling.
    pub fn effective_speed(&self, delta_secs: f32) -> f32 {
        if delta_secs <= f32::EPSILON {
            return 0.0;
        }
        let nominal = self.ant_speed();
        let max_safe = self.safe_step_distance() / delta_secs;
        nominal.min(max_safe)
    }

    /// Effective speed expressed as a multiplier of BASE_ANT_SPEED.
    pub fn effective_multiplier(&self, delta_secs: f32) -> f32 {
        self.effective_speed(delta_secs) / Config::BASE_ANT_SPEED
    }
}

/// Colors used in the simulation
pub struct Colors;

impl Colors {
    /// Color for ant sprites (light gray)
    pub const ANT: Color = Color::srgb(0.8, 0.8, 0.8);

    /// Color for food sprites (dark red)
    pub const FOOD: Color = Color::srgb(192.0 / 255.0, 2.0 / 255.0, 2.0 / 255.0);

    /// Background color (blue)
    pub const BACKGROUND: Color = Color::srgb(95.0 / 255.0, 151.0 / 255.0, 212.0 / 255.0);
}
