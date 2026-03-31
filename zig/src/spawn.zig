const std = @import("std");
const config = @import("config.zig");

pub const AntSpawn = struct {
    x: f32,
    y: f32,
    dir_x: f32,
    dir_y: f32,
};

pub const FoodPos = struct {
    x: f32,
    y: f32,
};

pub const max_ants = 256;
pub const max_food = 256;

pub const SpawnLayout = struct {
    ants_buf: [max_ants]AntSpawn = undefined,
    food_buf: [max_food]FoodPos = undefined,
    ant_count: usize = 0,
    food_count: usize = 0,

    pub fn ants(self: *const SpawnLayout) []const AntSpawn {
        return self.ants_buf[0..self.ant_count];
    }

    pub fn food(self: *const SpawnLayout) []const FoodPos {
        return self.food_buf[0..self.food_count];
    }
};

pub fn generateSpawnLayout(map_width: f32, map_height: f32, seed: u64) SpawnLayout {
    const half_x: i32 = @intFromFloat(map_width / 2.0);
    const half_y: i32 = @intFromFloat(map_height / 2.0);
    var rng = std.Random.DefaultPrng.init(seed);
    var random = rng.random();

    // Generate ants along horizontal line at y=100
    const step: i32 = config.ant_spawn_step;
    const range: i32 = half_x * 2;
    const ant_count: usize = @intCast(@divTrunc(range, step));
    var layout = SpawnLayout{};
    layout.ant_count = ant_count;

    for (0..ant_count) |i| {
        const x: f32 = @floatFromInt(-half_x + @as(i32, @intCast(i)) * step);
        const angle = random.float(f32) * 2.0 * std.math.pi;
        layout.ants_buf[i] = .{
            .x = x,
            .y = config.ant_spawn_y,
            .dir_x = @cos(angle),
            .dir_y = @sin(angle),
        };
    }

    // Generate food at random positions
    const fc: usize = @intCast(config.food_count);
    layout.food_count = fc;
    for (0..fc) |i| {
        const fx: i32 = random.intRangeAtMost(i32, -half_x, half_x);
        const fy: i32 = random.intRangeAtMost(i32, -half_y, half_y);
        layout.food_buf[i] = .{
            .x = @floatFromInt(fx),
            .y = @floatFromInt(fy),
        };
    }

    return layout;
}

// =============================================================================
// Tests
// =============================================================================

const testing = std.testing;

test "spawn layout generates correct ant count for 1280 wide map" {
    const layout = generateSpawnLayout(1280.0, 720.0, 42);
    try testing.expect(layout.ants().len > 0);
    try testing.expectEqual(@as(usize, 25), layout.ants().len);
}

test "all ants spawn at y=100" {
    const layout = generateSpawnLayout(1280.0, 720.0, 42);
    for (layout.ants()) |ant| {
        try testing.expectApproxEqAbs(@as(f32, 100.0), ant.y, 0.001);
    }
}

test "spawn layout generates 80 food items" {
    const layout = generateSpawnLayout(1280.0, 720.0, 42);
    try testing.expectEqual(@as(usize, 80), layout.food().len);
}

test "food positions are within map bounds" {
    const layout = generateSpawnLayout(1280.0, 720.0, 42);
    for (layout.food()) |pos| {
        try testing.expect(pos.x >= -640.0 and pos.x <= 640.0);
        try testing.expect(pos.y >= -360.0 and pos.y <= 360.0);
    }
}

test "ant directions are normalized (length ~1)" {
    const layout = generateSpawnLayout(1280.0, 720.0, 42);
    for (layout.ants()) |ant| {
        const len = @sqrt(ant.dir_x * ant.dir_x + ant.dir_y * ant.dir_y);
        try testing.expectApproxEqAbs(@as(f32, 1.0), len, 0.001);
    }
}

test "same seed produces identical layout" {
    const layout1 = generateSpawnLayout(1280.0, 720.0, 123);
    const layout2 = generateSpawnLayout(1280.0, 720.0, 123);

    try testing.expectEqual(layout1.ant_count, layout2.ant_count);
    for (layout1.ants(), layout2.ants()) |a, b| {
        try testing.expectEqual(a.x, b.x);
        try testing.expectEqual(a.y, b.y);
        try testing.expectEqual(a.dir_x, b.dir_x);
        try testing.expectEqual(a.dir_y, b.dir_y);
    }
    for (layout1.food(), layout2.food()) |a, b| {
        try testing.expectEqual(a.x, b.x);
        try testing.expectEqual(a.y, b.y);
    }
}

test "different seeds produce different layouts" {
    const layout1 = generateSpawnLayout(1280.0, 720.0, 1);
    const layout2 = generateSpawnLayout(1280.0, 720.0, 2);

    var any_diff = false;
    for (layout1.ants(), layout2.ants()) |a, b| {
        if (a.dir_x != b.dir_x or a.dir_y != b.dir_y) {
            any_diff = true;
            break;
        }
    }
    try testing.expect(any_diff);
}
