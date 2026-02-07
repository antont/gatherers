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
            speed_multiplier: 1.0, // Start at reasonable speed instead of max
        }
    }
}

impl SimulationSettings {
    /// Get current ant speed based on multiplier
    pub fn ant_speed(&self) -> f32 {
        // Note: For unlimited speed mode, actual speed is calculated dynamically
        // in gatherer_movement() based on delta time for optimal performance
        Config::BASE_ANT_SPEED * self.speed_multiplier
    }

    /// Check if we're in unlimited speed mode
    pub fn is_unlimited_speed(&self) -> bool {
        self.speed_multiplier == Config::UNLIMITED_SPEED
    }

    /// Get collision radius - always consistent for accurate simulation
    pub fn collision_radius(&self) -> f32 {
        // Keep collision radius consistent regardless of speed for simulation accuracy
        Config::BASE_COLLISION_RADIUS
    }

    /// Get maximum movement distance per frame to prevent tunneling
    /// This ensures ants can't move past food in a single frame
    pub fn max_movement_per_frame(&self) -> f32 {
        // Use the actual collision radius (which scales with speed) for movement limit
        // This ensures movement limit matches collision detection expectations
        let collision_radius = self.collision_radius();
        if self.is_unlimited_speed() {
            // More conservative limit for unlimited mode to prevent tunneling
            collision_radius * 0.3
        } else {
            // Normal limit: allow movement up to half the collision radius per frame
            collision_radius * 0.5
        }
    }

    /// Get cooldown time scaled for current speed

    /// Get cooldown distance squared - how far ant should move before being able to pick up food again
    /// Using squared distance to avoid expensive sqrt calculations
    /// Distance scales with current speed to maintain consistent cooldown time duration
    pub fn pickup_cooldown_distance_squared(&self) -> f32 {
        // Distance ant should move at current speed during base cooldown time, squared
        // This ensures cooldown duration stays constant regardless of speed setting
        let distance = self.ant_speed() * Config::BASE_PICKUP_COOLDOWN;
        distance * distance
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
