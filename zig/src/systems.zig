const std = @import("std");
const ecs = @import("zflecs");
const components = @import("components.zig");
const config = @import("config.zig");
const SpatialIndex = @import("spatial_index.zig").SpatialIndex;

const Position = components.Position;
const Velocity = components.Velocity;
const Bounding = components.Bounding;
const Cooldown = components.Cooldown;
const Carrying = components.Carrying;
const CarriedBy = components.CarriedBy;

// --- Boundary Wrap System ---

pub fn boundaryWrapSystem(world: *ecs.world_t, map_width: f32, map_height: f32) void {
    const half_w = map_width / 2.0;
    const half_h = map_height / 2.0;

    var desc = std.mem.zeroes(ecs.query_desc_t);
    desc.terms[0] = .{ .id = ecs.id(Position), .inout = .InOut };
    desc.terms[1] = .{ .id = ecs.id(Bounding), .inout = .In };
    desc.terms[2] = .{ .id = ecs.id(components.BoundaryWrap) };

    const query = ecs.query_init(world, &desc) catch return;
    defer ecs.query_fini(query);

    var it = ecs.query_iter(world, query);
    while (ecs.query_next(&it)) {
        const positions = ecs.field(&it, Position, 0).?;
        const boundings = ecs.field(&it, Bounding, 1).?;

        for (positions, boundings) |*pos, bnd| {
            const r = bnd.radius;
            if (pos.x - r > half_w) {
                pos.x = -half_w - r;
            } else if (pos.x + r < -half_w) {
                pos.x = half_w + r;
            }
            if (pos.y - r > half_h) {
                pos.y = -half_h - r;
            } else if (pos.y + r < -half_h) {
                pos.y = half_h + r;
            }
        }
    }
}

// --- Movement System ---

pub fn movementSystem(world: *ecs.world_t, settings: *const config.SimulationSettings, delta: f32) void {
    const speed = settings.effectiveSpeed(delta);
    const safe_step = settings.safeStepDistance();

    var desc = std.mem.zeroes(ecs.query_desc_t);
    desc.terms[0] = .{ .id = ecs.id(Position), .inout = .InOut };
    desc.terms[1] = .{ .id = ecs.id(Velocity), .inout = .In };
    desc.terms[2] = .{ .id = ecs.id(components.Ant) };

    const query = ecs.query_init(world, &desc) catch return;
    defer ecs.query_fini(query);

    var it = ecs.query_iter(world, query);
    while (ecs.query_next(&it)) {
        const positions = ecs.field(&it, Position, 0).?;
        const velocities = ecs.field(&it, Velocity, 1).?;

        for (positions, velocities) |*pos, vel| {
            const len = @sqrt(vel.x * vel.x + vel.y * vel.y);
            if (len < std.math.floatEps(f32)) continue;
            const dir_x = vel.x / len;
            const dir_y = vel.y / len;

            var move_x = dir_x * speed * delta;
            var move_y = dir_y * speed * delta;
            const move_dist = @sqrt(move_x * move_x + move_y * move_y);

            if (move_dist > safe_step and move_dist > 0) {
                move_x = move_x / move_dist * safe_step;
                move_y = move_y / move_dist * safe_step;
            }

            pos.x += move_x;
            pos.y += move_y;
        }
    }
}

// =============================================================================
// Test Helpers
// =============================================================================

fn createTestWorld() *ecs.world_t {
    const world = ecs.init();
    components.registerAll(world);
    return world;
}

fn spawnTestAnt(world: *ecs.world_t, x: f32, y: f32, dir_x: f32, dir_y: f32) ecs.entity_t {
    const e = ecs.new_id(world);
    ecs.add(world, e, components.Ant);
    _ = ecs.set(world, e, Position, .{ .x = x, .y = y });
    _ = ecs.set(world, e, Velocity, .{ .x = dir_x, .y = dir_y });
    _ = ecs.set(world, e, Bounding, .{ .radius = config.base_collision_radius });
    ecs.add(world, e, components.Collidable);
    ecs.add(world, e, components.BoundaryWrap);
    return e;
}

fn spawnTestFood(world: *ecs.world_t, x: f32, y: f32) ecs.entity_t {
    const e = ecs.new_id(world);
    ecs.add(world, e, components.Food);
    _ = ecs.set(world, e, Position, .{ .x = x, .y = y });
    _ = ecs.set(world, e, Bounding, .{ .radius = config.base_collision_radius });
    ecs.add(world, e, components.Collidable);
    return e;
}

// =============================================================================
// Movement System Tests
// =============================================================================

test "movement system moves ant by velocity * speed * delta" {
    const world = createTestWorld();
    defer _ = ecs.fini(world);

    const ant = spawnTestAnt(world, 0.0, 0.0, 1.0, 0.0);
    const settings = config.SimulationSettings{ .speed_multiplier = 1.0 };
    const delta: f32 = 1.0 / 60.0;

    movementSystem(world, &settings, delta);

    const pos = ecs.get(world, ant, Position).?;
    const expected_speed = settings.effectiveSpeed(delta);
    const expected_x = expected_speed * delta; // direction (1,0) * speed * delta
    try std.testing.expectApproxEqAbs(expected_x, pos.x, 0.01);
    try std.testing.expectApproxEqAbs(@as(f32, 0.0), pos.y, 0.01);
}

test "movement system caps to safe_step_distance (anti-tunneling)" {
    const world = createTestWorld();
    defer _ = ecs.fini(world);

    const ant = spawnTestAnt(world, 0.0, 0.0, 1.0, 0.0);
    const settings = config.SimulationSettings{ .speed_multiplier = config.unlimited_speed };
    const delta: f32 = 0.1;

    movementSystem(world, &settings, delta);

    const pos = ecs.get(world, ant, Position).?;
    const dist = @sqrt(pos.x * pos.x + pos.y * pos.y);
    try std.testing.expect(dist <= settings.safeStepDistance() + 0.01);
}

test "movement system does nothing with zero delta" {
    const world = createTestWorld();
    defer _ = ecs.fini(world);

    const ant = spawnTestAnt(world, 100.0, 200.0, 1.0, 0.0);
    const settings = config.SimulationSettings{};

    movementSystem(world, &settings, 0.0);

    const pos = ecs.get(world, ant, Position).?;
    try std.testing.expectEqual(@as(f32, 100.0), pos.x);
    try std.testing.expectEqual(@as(f32, 200.0), pos.y);
}

// =============================================================================
// Boundary Wrap System Tests
// =============================================================================

test "boundary wrap: ant past right edge wraps to left" {
    const world = createTestWorld();
    defer _ = ecs.fini(world);

    // Map 1280x720: half_width = 640
    // Ant at x=651 with radius 10: 651-10=641 > 640 → wraps
    const ant = spawnTestAnt(world, 651.0, 0.0, 1.0, 0.0);

    boundaryWrapSystem(world, config.map_width, config.map_height);

    const pos = ecs.get(world, ant, Position).?;
    try std.testing.expect(pos.x < 0); // Should have wrapped to left side
}

test "boundary wrap: ant past left edge wraps to right" {
    const world = createTestWorld();
    defer _ = ecs.fini(world);

    // Ant at x=-651 with radius 10: -651+10=-641 < -640 → wraps
    const ant = spawnTestAnt(world, -651.0, 0.0, 1.0, 0.0);

    boundaryWrapSystem(world, config.map_width, config.map_height);

    const pos = ecs.get(world, ant, Position).?;
    try std.testing.expect(pos.x > 0); // Should have wrapped to right side
}

test "boundary wrap: ant within bounds unchanged" {
    const world = createTestWorld();
    defer _ = ecs.fini(world);

    const ant = spawnTestAnt(world, 100.0, 200.0, 1.0, 0.0);

    boundaryWrapSystem(world, config.map_width, config.map_height);

    const pos = ecs.get(world, ant, Position).?;
    try std.testing.expectEqual(@as(f32, 100.0), pos.x);
    try std.testing.expectEqual(@as(f32, 200.0), pos.y);
}

test "boundary wrap: vertical wrapping" {
    const world = createTestWorld();
    defer _ = ecs.fini(world);

    // half_height = 360, ant at y=371 with radius 10: 371-10=361 > 360 → wraps
    const ant = spawnTestAnt(world, 0.0, 371.0, 1.0, 0.0);

    boundaryWrapSystem(world, config.map_width, config.map_height);

    const pos = ecs.get(world, ant, Position).?;
    try std.testing.expect(pos.y < 0); // Wrapped to bottom
}

// =============================================================================
// Collision Detection Tests
// =============================================================================

test "collision: ant near food produces hit event" {
    const world = createTestWorld();
    defer _ = ecs.fini(world);

    const ant = spawnTestAnt(world, 5.0, 0.0, 1.0, 0.0);
    const food = spawnTestFood(world, 10.0, 0.0);

    var si = SpatialIndex.init(std.testing.allocator);
    defer si.deinit();
    var hits = std.ArrayList(HitEvent).init(std.testing.allocator);
    defer hits.deinit();

    // Build spatial index with food
    const food_pos = ecs.get(world, food, Position).?;
    try si.insert(food, food_pos.x, food_pos.y);

    collisionDetectionSystem(world, &si, &hits);

    try std.testing.expectEqual(@as(usize, 1), hits.items.len);
    try std.testing.expectEqual(ant, hits.items[0].ant);
    try std.testing.expectEqual(food, hits.items[0].food);
}

test "collision: ant far from food produces no hit" {
    const world = createTestWorld();
    defer _ = ecs.fini(world);

    _ = spawnTestAnt(world, 0.0, 0.0, 1.0, 0.0);
    const food = spawnTestFood(world, 500.0, 500.0);

    var si = SpatialIndex.init(std.testing.allocator);
    defer si.deinit();
    var hits = std.ArrayList(HitEvent).init(std.testing.allocator);
    defer hits.deinit();

    const food_pos = ecs.get(world, food, Position).?;
    try si.insert(food, food_pos.x, food_pos.y);

    collisionDetectionSystem(world, &si, &hits);

    try std.testing.expectEqual(@as(usize, 0), hits.items.len);
}

test "collision: only one hit per ant per frame" {
    const world = createTestWorld();
    defer _ = ecs.fini(world);

    _ = spawnTestAnt(world, 0.0, 0.0, 1.0, 0.0);
    const food1 = spawnTestFood(world, 5.0, 0.0);
    const food2 = spawnTestFood(world, -5.0, 0.0);

    var si = SpatialIndex.init(std.testing.allocator);
    defer si.deinit();
    var hits = std.ArrayList(HitEvent).init(std.testing.allocator);
    defer hits.deinit();

    const f1_pos = ecs.get(world, food1, Position).?;
    const f2_pos = ecs.get(world, food2, Position).?;
    try si.insert(food1, f1_pos.x, f1_pos.y);
    try si.insert(food2, f2_pos.x, f2_pos.y);

    collisionDetectionSystem(world, &si, &hits);

    // Should only get 1 hit even though 2 foods are nearby
    try std.testing.expectEqual(@as(usize, 1), hits.items.len);
}
