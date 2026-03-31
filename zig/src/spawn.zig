const std = @import("std");

// Spawn layout generation — to be implemented

// =============================================================================
// Tests
// =============================================================================

const testing = std.testing;

test "spawn layout generates correct ant count for 1280 wide map" {
    const spawn = @import("spawn.zig");
    // Map width 1280: half_x = 640, range -640..640, step 50 → 1280/50 = 25 ants (matches Rust)
    const layout = spawn.generateSpawnLayout(1280.0, 720.0, 42);
    try testing.expect(layout.ants.len > 0);
    try testing.expectEqual(@as(usize, 25), layout.ants.len);
}

test "all ants spawn at y=100" {
    const spawn = @import("spawn.zig");
    const layout = spawn.generateSpawnLayout(1280.0, 720.0, 42);
    for (layout.ants) |ant| {
        try testing.expectApproxEqAbs(@as(f32, 100.0), ant.y, 0.001);
    }
}

test "spawn layout generates 80 food items" {
    const spawn = @import("spawn.zig");
    const layout = spawn.generateSpawnLayout(1280.0, 720.0, 42);
    try testing.expectEqual(@as(usize, 80), layout.food.len);
}

test "food positions are within map bounds" {
    const spawn = @import("spawn.zig");
    const layout = spawn.generateSpawnLayout(1280.0, 720.0, 42);
    for (layout.food) |pos| {
        try testing.expect(pos.x >= -640.0 and pos.x <= 640.0);
        try testing.expect(pos.y >= -360.0 and pos.y <= 360.0);
    }
}

test "ant directions are normalized (length ~1)" {
    const spawn = @import("spawn.zig");
    const layout = spawn.generateSpawnLayout(1280.0, 720.0, 42);
    for (layout.ants) |ant| {
        const len = @sqrt(ant.dir_x * ant.dir_x + ant.dir_y * ant.dir_y);
        try testing.expectApproxEqAbs(@as(f32, 1.0), len, 0.001);
    }
}

test "same seed produces identical layout" {
    const spawn = @import("spawn.zig");
    const layout1 = spawn.generateSpawnLayout(1280.0, 720.0, 123);
    const layout2 = spawn.generateSpawnLayout(1280.0, 720.0, 123);

    try testing.expectEqual(layout1.ants.len, layout2.ants.len);
    for (layout1.ants, layout2.ants) |a, b| {
        try testing.expectEqual(a.x, b.x);
        try testing.expectEqual(a.y, b.y);
        try testing.expectEqual(a.dir_x, b.dir_x);
        try testing.expectEqual(a.dir_y, b.dir_y);
    }
    for (layout1.food, layout2.food) |a, b| {
        try testing.expectEqual(a.x, b.x);
        try testing.expectEqual(a.y, b.y);
    }
}

test "different seeds produce different layouts" {
    const spawn = @import("spawn.zig");
    const layout1 = spawn.generateSpawnLayout(1280.0, 720.0, 1);
    const layout2 = spawn.generateSpawnLayout(1280.0, 720.0, 2);

    // At least one ant direction should differ
    var any_diff = false;
    for (layout1.ants, layout2.ants) |a, b| {
        if (a.dir_x != b.dir_x or a.dir_y != b.dir_y) {
            any_diff = true;
            break;
        }
    }
    try testing.expect(any_diff);
}
