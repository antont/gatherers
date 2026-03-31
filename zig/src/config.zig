const std = @import("std");

// --- Constants (mirrors src/config.rs) ---

pub const ant_size_w: f32 = 20.0;
pub const ant_size_h: f32 = 20.0;
pub const food_size_w: f32 = 10.0;
pub const food_size_h: f32 = 10.0;
pub const ant_spawn_step: i32 = 50;
pub const ant_spawn_y: f32 = 100.0;
pub const food_count: i32 = 80;
pub const turn_angle_range: f32 = std.math.pi / 2.0;
pub const spatial_cell_size: f32 = 20.0;
pub const base_ant_speed: f32 = 100.0;
pub const base_pickup_cooldown: f32 = 1.0;
pub const base_collision_radius: f32 = 10.0;
pub const map_width: f32 = 1280.0;
pub const map_height: f32 = 720.0;

pub const min_speed_multiplier: f32 = 0.1;
pub const max_speed_multiplier: f32 = 10.0;
pub const unlimited_speed: f32 = -1.0;

// --- SimulationSettings ---

pub const SimulationSettings = struct {
    speed_multiplier: f32 = 5.0,

    pub fn isUnlimitedSpeed(self: SimulationSettings) bool {
        return self.speed_multiplier == unlimited_speed;
    }

    pub fn collisionRadius(_: SimulationSettings) f32 {
        return base_collision_radius;
    }

    pub fn safeStepDistance(self: SimulationSettings) f32 {
        return self.collisionRadius() * 2.0 * 0.9;
    }

    pub fn antSpeed(self: SimulationSettings) f32 {
        if (self.isUnlimitedSpeed()) {
            return base_ant_speed * max_speed_multiplier * 20.0;
        }
        return base_ant_speed * self.speed_multiplier;
    }

    pub fn effectiveSpeed(self: SimulationSettings, delta_secs: f32) f32 {
        if (delta_secs <= std.math.floatEps(f32)) {
            return 0.0;
        }
        const nominal = self.antSpeed();
        const max_safe = self.safeStepDistance() / delta_secs;
        return @min(nominal, max_safe);
    }

    pub fn effectiveMultiplier(self: SimulationSettings, delta_secs: f32) f32 {
        return self.effectiveSpeed(delta_secs) / base_ant_speed;
    }
};

// =============================================================================
// Tests
// =============================================================================

test "config constants match Rust values" {
    const cfg = @import("config.zig");
    try std.testing.expectEqual(@as(f32, 20.0), cfg.ant_size_w);
    try std.testing.expectEqual(@as(f32, 20.0), cfg.ant_size_h);
    try std.testing.expectEqual(@as(f32, 10.0), cfg.food_size_w);
    try std.testing.expectEqual(@as(f32, 10.0), cfg.food_size_h);
    try std.testing.expectEqual(@as(i32, 50), cfg.ant_spawn_step);
    try std.testing.expectEqual(@as(f32, 100.0), cfg.ant_spawn_y);
    try std.testing.expectEqual(@as(i32, 80), cfg.food_count);
    try std.testing.expectApproxEqAbs(std.math.pi / 2.0, cfg.turn_angle_range, 0.001);
    try std.testing.expectEqual(@as(f32, 20.0), cfg.spatial_cell_size);
    try std.testing.expectEqual(@as(f32, 100.0), cfg.base_ant_speed);
    try std.testing.expectEqual(@as(f32, 1.0), cfg.base_pickup_cooldown);
    try std.testing.expectEqual(@as(f32, 10.0), cfg.base_collision_radius);
}

test "SimulationSettings.safe_step_distance is 0.9 * 2 * collision_radius" {
    const cfg = @import("config.zig");
    const settings = cfg.SimulationSettings{};
    try std.testing.expectApproxEqAbs(@as(f32, 18.0), settings.safeStepDistance(), 0.001);
}

test "SimulationSettings.effective_speed caps to prevent tunneling" {
    const cfg = @import("config.zig");
    const settings = cfg.SimulationSettings{ .speed_multiplier = 10.0 };
    const speed_60fps = settings.effectiveSpeed(1.0 / 60.0);
    try std.testing.expectApproxEqAbs(@as(f32, 1000.0), speed_60fps, 0.1);

    const speed_10fps = settings.effectiveSpeed(0.1);
    try std.testing.expectApproxEqAbs(@as(f32, 180.0), speed_10fps, 0.1);
}

test "SimulationSettings.effective_speed returns 0 for zero delta" {
    const cfg = @import("config.zig");
    const settings = cfg.SimulationSettings{};
    try std.testing.expectEqual(@as(f32, 0.0), settings.effectiveSpeed(0.0));
}

test "SimulationSettings.effective_speed unlimited mode" {
    const cfg = @import("config.zig");
    const settings = cfg.SimulationSettings{ .speed_multiplier = cfg.unlimited_speed };
    const speed = settings.effectiveSpeed(1.0 / 60.0);
    const expected = @as(f32, 18.0) / (1.0 / 60.0);
    try std.testing.expectApproxEqAbs(expected, speed, 0.1);
}

test "SimulationSettings default speed_multiplier is 5.0" {
    const cfg = @import("config.zig");
    const settings = cfg.SimulationSettings{};
    try std.testing.expectEqual(@as(f32, 5.0), settings.speed_multiplier);
}
