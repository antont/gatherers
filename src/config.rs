//! Configuration constants for the gatherers simulation.
//! Contains all configurable parameters in one place to avoid magic numbers throughout the codebase.

use bevy::prelude::*;

/// Configuration constants for the simulation.
/// All values are compile-time constants for optimal performance.
pub struct Config;

impl Config {
    /// Size of ant sprites
    pub const ANT_SIZE: Vec2 = Vec2::new(20.0, 20.0);

    /// Size of food sprites
    pub const FOOD_SIZE: Vec2 = Vec2::new(10.0, 10.0);

    /// Speed multiplier for ant movement
    pub const ANT_SPEED: f32 = 1000.0;

    /// Step size for ant spawning grid (controls ant density)
    pub const ANT_SPAWN_STEP: i32 = 50;

    /// Y position where ants are spawned
    pub const ANT_SPAWN_Y: f32 = 100.0;

    /// Number of food items to spawn
    pub const FOOD_COUNT: i32 = 80;

    /// Cooldown time after picking up/dropping food (in seconds)
    pub const PICKUP_COOLDOWN: f32 = 0.1;

    /// Maximum random angle range when turning (in radians, ±90 degrees)
    pub const TURN_ANGLE_RANGE: f32 = std::f32::consts::FRAC_PI_2;

    /// Cell size for spatial indexing collision detection
    /// Should be larger than collision radius for optimal performance
    pub const SPATIAL_CELL_SIZE: f32 = 20.0;

    /// Radius for bounding collision detection
    pub const COLLISION_RADIUS: f32 = 10.0;

    /// Z-layer for ants
    pub const ANT_Z_LAYER: f32 = 2.0;

    /// Z-layer for food on ground
    pub const FOOD_Z_LAYER: f32 = 1.0;

    /// Z-layer for carried food (should be above ants)
    pub const CARRIED_FOOD_Z_LAYER: f32 = 3.0;
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
