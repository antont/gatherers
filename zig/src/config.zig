const std = @import("std");

// --- Constants (to be implemented) ---

// --- SimulationSettings (to be implemented) ---

// =============================================================================
// Tests
// =============================================================================

test "config constants match Rust values" {
    // Mirrors src/config.rs Config struct
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
    // From Rust: collision_radius() * 2.0 * 0.9 = 10.0 * 2.0 * 0.9 = 18.0
    try std.testing.expectApproxEqAbs(@as(f32, 18.0), settings.safeStepDistance(), 0.001);
}

test "SimulationSettings.effective_speed caps to prevent tunneling" {
    const cfg = @import("config.zig");
    const settings = cfg.SimulationSettings{ .speed_multiplier = 10.0 };
    // At 10x speed: nominal = 100 * 10 = 1000 units/sec
    // At delta = 1/60: desired step = 1000/60 ≈ 16.67 < safe_step 18 → no cap
    const speed_60fps = settings.effectiveSpeed(1.0 / 60.0);
    try std.testing.expectApproxEqAbs(@as(f32, 1000.0), speed_60fps, 0.1);

    // At delta = 0.1 (10 FPS): desired step = 1000 * 0.1 = 100 >> 18 → capped
    // max_safe = 18.0 / 0.1 = 180.0
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
    // Unlimited: nominal = 100 * 10 * 20 = 20000
    // At delta = 1/60: step = 20000/60 ≈ 333 >> 18 → capped to 18/delta = 1080
    const speed = settings.effectiveSpeed(1.0 / 60.0);
    const expected = @as(f32, 18.0) / (1.0 / 60.0);
    try std.testing.expectApproxEqAbs(expected, speed, 0.1);
}

test "SimulationSettings default speed_multiplier is 5.0" {
    const cfg = @import("config.zig");
    const settings = cfg.SimulationSettings{};
    try std.testing.expectEqual(@as(f32, 5.0), settings.speed_multiplier);
}
