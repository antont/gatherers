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

// --- Systems (to be implemented) ---

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

    _ = spawnTestAnt(world, 0.0, 0.0, 1.0, 0.0);
    // Unlimited speed + large delta → should cap movement
    const settings = config.SimulationSettings{ .speed_multiplier = config.unlimited_speed };
    const delta: f32 = 0.1;

    movementSystem(world, &settings, delta);

    // Movement should be capped to safe_step_distance = 18.0
    const ant_query = ecs.query_init(world, &.{
        .terms = .{
            .{ .id = ecs.id(Position), .inout = .InOut },
            .{ .id = ecs.id(components.Ant) },
        } ++ .{.{}} ** 14,
    }) orelse unreachable;
    defer ecs.query_fini(ant_query);

    var it = ecs.query_iter(world, ant_query);
    while (ecs.query_next(&it)) {
        const positions = ecs.field(&it, Position, 0).?;
        for (positions) |pos| {
            const dist = @sqrt(pos.x * pos.x + pos.y * pos.y);
            try std.testing.expect(dist <= settings.safeStepDistance() + 0.01);
        }
    }
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
