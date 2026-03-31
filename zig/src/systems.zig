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

// --- Hit Event ---

pub const HitEvent = struct {
    food: ecs.entity_t,
    ant: ecs.entity_t,
};

// --- Ant Hits System ---

pub fn antHitsSystem(world: *ecs.world_t, hits: []const HitEvent, random: std.Random) void {
    for (hits) |hit| {
        const ant = hit.ant;

        // Skip ants with cooldown
        if (ecs.get(world, ant, Cooldown) != null) continue;

        // Get ant position and velocity
        const ant_pos = ecs.get(world, ant, Position) orelse continue;
        const ant_vel = ecs.get(world, ant, Velocity) orelse continue;

        // Check if ant is currently carrying food
        const carrying = ecs.get(world, ant, Carrying);

        if (carrying != null) {
            // --- DROP carried food ---
            const carried_food = carrying.?.food;

            // Set food position to ant's position
            _ = ecs.set(world, carried_food, Position, .{
                .x = ant_pos.x,
                .y = ant_pos.y,
            });

            // Remove carrying/carried-by relationships
            ecs.remove(world, ant, Carrying);
            ecs.remove(world, carried_food, CarriedBy);

            // Re-add collidable to dropped food
            ecs.add(world, carried_food, components.Collidable);

            // Add cooldown to ant
            _ = ecs.set(world, ant, Cooldown, .{ .timer = config.base_pickup_cooldown });
        } else {
            // --- PICKUP food ---
            const food = hit.food;

            // Set carrying relationship
            _ = ecs.set(world, ant, Carrying, .{ .food = food });
            _ = ecs.set(world, food, CarriedBy, .{ .ant = ant });

            // Remove collidable from picked-up food
            ecs.remove(world, food, components.Collidable);
        }

        // Turn: reverse direction + random angle (±turn_angle_range)
        const current_angle = std.math.atan2(ant_vel.y, ant_vel.x);
        const random_offset = random.float(f32) * 2.0 * config.turn_angle_range - config.turn_angle_range;
        const new_angle = current_angle + std.math.pi + random_offset;
        _ = ecs.set(world, ant, Velocity, .{
            .x = @cos(new_angle),
            .y = @sin(new_angle),
        });
    }
}

// --- Cooldown System ---

pub fn cooldownSystem(world: *ecs.world_t, delta: f32) void {
    var desc = std.mem.zeroes(ecs.query_desc_t);
    desc.terms[0] = .{ .id = ecs.id(Cooldown), .inout = .InOut };
    desc.terms[1] = .{ .id = ecs.id(components.Ant) };

    const query = ecs.query_init(world, &desc) catch return;
    defer ecs.query_fini(query);

    var it = ecs.query_iter(world, query);
    while (ecs.query_next(&it)) {
        const cooldowns = ecs.field(&it, Cooldown, 0).?;
        const entities = it.entities();

        for (cooldowns, entities) |*cd, entity| {
            cd.timer -= delta;
            if (cd.timer <= 0) {
                ecs.remove(world, entity, Cooldown);
            }
        }
    }
}

// --- Spatial Index Update System ---

pub fn updateSpatialIndexSystem(world: *ecs.world_t, si: *SpatialIndex) void {
    si.clear();

    var desc = std.mem.zeroes(ecs.query_desc_t);
    desc.terms[0] = .{ .id = ecs.id(Position), .inout = .In };
    desc.terms[1] = .{ .id = ecs.id(components.Food) };
    desc.terms[2] = .{ .id = ecs.id(components.Collidable) };

    const query = ecs.query_init(world, &desc) catch return;
    defer ecs.query_fini(query);

    var it = ecs.query_iter(world, query);
    while (ecs.query_next(&it)) {
        const positions = ecs.field(&it, Position, 0).?;
        const entities = it.entities();

        for (positions, entities) |pos, entity| {
            si.insert(entity, pos.x, pos.y) catch continue;
        }
    }
}

// --- Collision Detection System ---

pub fn collisionDetectionSystem(world: *ecs.world_t, si: *SpatialIndex, hits: *std.ArrayList(HitEvent), allocator: std.mem.Allocator) void {
    var desc = std.mem.zeroes(ecs.query_desc_t);
    desc.terms[0] = .{ .id = ecs.id(Position), .inout = .In };
    desc.terms[1] = .{ .id = ecs.id(Bounding), .inout = .In };
    desc.terms[2] = .{ .id = ecs.id(components.Ant) };
    desc.terms[3] = .{ .id = ecs.id(components.Collidable) };

    const query = ecs.query_init(world, &desc) catch return;
    defer ecs.query_fini(query);

    var it = ecs.query_iter(world, query);
    while (ecs.query_next(&it)) {
        const positions = ecs.field(&it, Position, 0).?;
        const boundings = ecs.field(&it, Bounding, 1).?;
        const entities = it.entities();

        for (positions, boundings, entities) |ant_pos, ant_bnd, ant_entity| {
            const nearby = si.getNearby(ant_pos.x, ant_pos.y) catch continue;
            defer si.allocator.free(nearby);

            for (nearby) |food_entity| {
                // Get food position and bounding from the world
                const food_pos = ecs.get(world, food_entity, Position) orelse continue;
                const food_bnd = ecs.get(world, food_entity, Bounding) orelse continue;

                // Check if food is still collidable
                if (!ecs.has_id(world, food_entity, ecs.id(components.Collidable))) continue;

                const dx = food_pos.x - ant_pos.x;
                const dy = food_pos.y - ant_pos.y;
                const dist_sq = dx * dx + dy * dy;
                const radius_sum = ant_bnd.radius + food_bnd.radius;
                const coll_dist_sq = radius_sum * radius_sum;

                if (dist_sq < coll_dist_sq) {
                    hits.append(allocator, .{ .food = food_entity, .ant = ant_entity }) catch continue;
                    break; // One collision per ant per frame
                }
            }
        }
    }
}

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

test "movement system updates carried food position to match ant" {
    const world = createTestWorld();
    defer _ = ecs.fini(world);

    const ant = spawnTestAnt(world, 0.0, 0.0, 1.0, 0.0);
    const food = spawnTestFood(world, 99.0, 99.0); // food starts elsewhere

    // Set up carrying relationship
    _ = ecs.set(world, ant, Carrying, .{ .food = food });
    _ = ecs.set(world, food, CarriedBy, .{ .ant = ant });
    ecs.remove(world, food, components.Collidable);

    const settings = config.SimulationSettings{ .speed_multiplier = 1.0 };
    const delta: f32 = 1.0 / 60.0;

    movementSystem(world, &settings, delta);

    // Carried food should now be at the ant's new position
    const ant_pos = ecs.get(world, ant, Position).?;
    const food_pos = ecs.get(world, food, Position).?;
    try std.testing.expectApproxEqAbs(ant_pos.x, food_pos.x, 0.01);
    try std.testing.expectApproxEqAbs(ant_pos.y, food_pos.y, 0.01);
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
    var hits: std.ArrayList(HitEvent) = .{};
    defer hits.deinit(std.testing.allocator);

    // Build spatial index with food
    const food_pos = ecs.get(world, food, Position).?;
    try si.insert(food, food_pos.x, food_pos.y);

    collisionDetectionSystem(world, &si, &hits, std.testing.allocator);

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
    var hits: std.ArrayList(HitEvent) = .{};
    defer hits.deinit(std.testing.allocator);

    const food_pos = ecs.get(world, food, Position).?;
    try si.insert(food, food_pos.x, food_pos.y);

    collisionDetectionSystem(world, &si, &hits, std.testing.allocator);

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
    var hits: std.ArrayList(HitEvent) = .{};
    defer hits.deinit(std.testing.allocator);

    const f1_pos = ecs.get(world, food1, Position).?;
    const f2_pos = ecs.get(world, food2, Position).?;
    try si.insert(food1, f1_pos.x, f1_pos.y);
    try si.insert(food2, f2_pos.x, f2_pos.y);

    collisionDetectionSystem(world, &si, &hits, std.testing.allocator);

    // Should only get 1 hit even though 2 foods are nearby
    try std.testing.expectEqual(@as(usize, 1), hits.items.len);
}

// =============================================================================
// Ant Hits System Tests (pickup / drop)
// =============================================================================

test "ant hits: pickup — ant without food picks up food" {
    const world = createTestWorld();
    defer _ = ecs.fini(world);

    const ant = spawnTestAnt(world, 5.0, 0.0, 1.0, 0.0);
    const food = spawnTestFood(world, 10.0, 0.0);

    var rng = std.Random.DefaultPrng.init(42);

    const hit = HitEvent{ .ant = ant, .food = food };
    antHitsSystem(world, &[_]HitEvent{hit}, rng.random());

    // Ant should now be carrying this food
    const carrying = ecs.get(world, ant, Carrying);
    try std.testing.expect(carrying != null);
    try std.testing.expectEqual(food, carrying.?.food);

    // Food should have CarriedBy and lose Collidable
    const carried_by = ecs.get(world, food, CarriedBy);
    try std.testing.expect(carried_by != null);
    try std.testing.expectEqual(ant, carried_by.?.ant);
    try std.testing.expect(!ecs.has_id(world, food, ecs.id(components.Collidable)));

    // Ant velocity should have changed (turned ~180 degrees)
    const vel = ecs.get(world, ant, Velocity).?;
    // Original direction was (1, 0), reversed would be roughly (-1, 0) ± random
    // Just check it's different from original
    try std.testing.expect(vel.x != 1.0 or vel.y != 0.0);
}

test "ant hits: drop — ant carrying food drops it on new collision" {
    const world = createTestWorld();
    defer _ = ecs.fini(world);

    const ant = spawnTestAnt(world, 5.0, 0.0, 1.0, 0.0);
    const carried_food = spawnTestFood(world, 0.0, 0.0);
    const new_food = spawnTestFood(world, 10.0, 0.0);

    // Manually set ant as carrying
    _ = ecs.set(world, ant, Carrying, .{ .food = carried_food });
    _ = ecs.set(world, carried_food, CarriedBy, .{ .ant = ant });
    ecs.remove(world, carried_food, components.Collidable);

    var rng = std.Random.DefaultPrng.init(42);

    const hit = HitEvent{ .ant = ant, .food = new_food };
    antHitsSystem(world, &[_]HitEvent{hit}, rng.random());

    // Ant should no longer be carrying
    const carrying = ecs.get(world, ant, Carrying);
    try std.testing.expect(carrying == null);

    // Carried food should be dropped: CarriedBy removed, Collidable re-added
    const carried_by = ecs.get(world, carried_food, CarriedBy);
    try std.testing.expect(carried_by == null);
    try std.testing.expect(ecs.has_id(world, carried_food, ecs.id(components.Collidable)));

    // Dropped food gets ant's position
    const food_pos = ecs.get(world, carried_food, Position).?;
    try std.testing.expectApproxEqAbs(@as(f32, 5.0), food_pos.x, 0.01);
    try std.testing.expectApproxEqAbs(@as(f32, 0.0), food_pos.y, 0.01);

    // Ant should have cooldown
    const cooldown = ecs.get(world, ant, Cooldown);
    try std.testing.expect(cooldown != null);
    try std.testing.expectApproxEqAbs(config.base_pickup_cooldown, cooldown.?.timer, 0.01);
}

test "ant hits: ant with cooldown ignores hits" {
    const world = createTestWorld();
    defer _ = ecs.fini(world);

    const ant = spawnTestAnt(world, 5.0, 0.0, 1.0, 0.0);
    _ = ecs.set(world, ant, Cooldown, .{ .timer = 0.5 });
    const food = spawnTestFood(world, 10.0, 0.0);

    var rng = std.Random.DefaultPrng.init(42);

    const hit = HitEvent{ .ant = ant, .food = food };
    antHitsSystem(world, &[_]HitEvent{hit}, rng.random());

    // Ant should NOT be carrying — cooldown blocks pickup
    const carrying = ecs.get(world, ant, Carrying);
    try std.testing.expect(carrying == null);

    // Food should still be collidable (not picked up)
    try std.testing.expect(ecs.has_id(world, food, ecs.id(components.Collidable)));
}

// =============================================================================
// Cooldown System Tests
// =============================================================================

test "cooldown: timer decrements by delta" {
    const world = createTestWorld();
    defer _ = ecs.fini(world);

    const ant = spawnTestAnt(world, 0.0, 0.0, 1.0, 0.0);
    _ = ecs.set(world, ant, Cooldown, .{ .timer = 1.0 });

    cooldownSystem(world, 0.3);

    const cd = ecs.get(world, ant, Cooldown).?;
    try std.testing.expectApproxEqAbs(@as(f32, 0.7), cd.timer, 0.01);
}

test "cooldown: removed when timer expires" {
    const world = createTestWorld();
    defer _ = ecs.fini(world);

    const ant = spawnTestAnt(world, 0.0, 0.0, 1.0, 0.0);
    _ = ecs.set(world, ant, Cooldown, .{ .timer = 0.5 });

    cooldownSystem(world, 0.6); // 0.5 - 0.6 = -0.1 → should be removed

    const cd = ecs.get(world, ant, Cooldown);
    try std.testing.expect(cd == null);
}

test "cooldown: multiple ticks to expire" {
    const world = createTestWorld();
    defer _ = ecs.fini(world);

    const ant = spawnTestAnt(world, 0.0, 0.0, 1.0, 0.0);
    _ = ecs.set(world, ant, Cooldown, .{ .timer = 1.0 });

    cooldownSystem(world, 0.3); // 1.0 → 0.7
    try std.testing.expect(ecs.get(world, ant, Cooldown) != null);

    cooldownSystem(world, 0.3); // 0.7 → 0.4
    try std.testing.expect(ecs.get(world, ant, Cooldown) != null);

    cooldownSystem(world, 0.3); // 0.4 → 0.1
    try std.testing.expect(ecs.get(world, ant, Cooldown) != null);

    cooldownSystem(world, 0.3); // 0.1 → -0.2 → removed
    try std.testing.expect(ecs.get(world, ant, Cooldown) == null);
}
