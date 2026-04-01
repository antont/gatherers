const std = @import("std");
const ecs = @import("zflecs");
const components = @import("components.zig");
const config = @import("config.zig");
const spawn = @import("spawn.zig");
const systems = @import("systems.zig");
const SpatialIndex = @import("spatial_index.zig").SpatialIndex;

const Position = components.Position;

pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    const world = ecs.init();
    defer _ = ecs.fini(world);

    components.registerAll(world);

    // Parse seed from env or use default
    const seed: u64 = blk: {
        const seed_str = std.process.getEnvVarOwned(allocator, "GATHERERS_SIM_SEED") catch break :blk 42;
        defer allocator.free(seed_str);
        break :blk std.fmt.parseInt(u64, seed_str, 10) catch 42;
    };

    // Spawn entities
    const layout = spawn.generateSpawnLayout(config.map_width, config.map_height, seed);
    for (layout.ants()) |ant| {
        const e = ecs.new_id(world);
        ecs.add(world, e, components.Ant);
        _ = ecs.set(world, e, Position, .{ .x = ant.x, .y = ant.y });
        _ = ecs.set(world, e, components.Velocity, .{ .x = ant.dir_x, .y = ant.dir_y });
        _ = ecs.set(world, e, components.Bounding, .{ .radius = config.base_collision_radius });
        ecs.add(world, e, components.Collidable);
        ecs.add(world, e, components.BoundaryWrap);
    }
    for (layout.food()) |food| {
        const e = ecs.new_id(world);
        ecs.add(world, e, components.Food);
        _ = ecs.set(world, e, Position, .{ .x = food.x, .y = food.y });
        _ = ecs.set(world, e, components.Bounding, .{ .radius = config.base_collision_radius });
        ecs.add(world, e, components.Collidable);
    }

    std.debug.print("zig-gatherers: spawned {} ants and {} food (seed={})\n", .{
        layout.ant_count,
        layout.food_count,
        seed,
    });

    // Simulation state
    const settings = config.SimulationSettings{ .speed_multiplier = 5.0 };
    var si = SpatialIndex.init(allocator);
    defer si.deinit();
    var hits: std.ArrayList(systems.HitEvent) = .{};
    defer hits.deinit(allocator);

    var rng = std.Random.DefaultPrng.init(seed +% 1);
    const delta: f32 = 1.0 / 60.0; // Fixed timestep

    // Main simulation loop
    const max_frames: u64 = 5000;
    var frame: u64 = 0;

    while (frame < max_frames) : (frame += 1) {
        // Clear hit queue
        hits.clearRetainingCapacity();

        // Movement
        systems.movementSystem(world, &settings, delta);

        // Update spatial index with collidable food
        systems.updateSpatialIndexSystem(world, &si);

        // Collision detection
        systems.collisionDetectionSystem(world, &si, &hits, allocator);

        // Process hits
        if (hits.items.len > 0) {
            systems.antHitsSystem(world, hits.items, rng.random());
        }

        // Cooldown
        systems.cooldownSystem(world, delta);

        // Boundary wrapping
        systems.boundaryWrapSystem(world, config.map_width, config.map_height);

        // Periodic output every 500 frames
        if (frame % 500 == 0) {
            const stats = countStats(world);
            std.debug.print("frame {d:>5}: food_ground={d:>3} carried={d:>3} cooldowns={d:>3}\n", .{
                frame,
                stats.food_on_ground,
                stats.food_carried,
                stats.ants_with_cooldown,
            });
        }
    }

    const final = countStats(world);
    std.debug.print("\nSimulation complete after {} frames.\n", .{max_frames});
    std.debug.print("Final: food_ground={} carried={} total_food={}\n", .{
        final.food_on_ground,
        final.food_carried,
        final.food_on_ground + final.food_carried,
    });
}

const Stats = struct {
    food_on_ground: u32,
    food_carried: u32,
    ants_with_cooldown: u32,
};

fn countStats(world: *ecs.world_t) Stats {
    var stats = Stats{ .food_on_ground = 0, .food_carried = 0, .ants_with_cooldown = 0 };

    // Count food on ground (Food + Collidable)
    {
        var desc = std.mem.zeroes(ecs.query_desc_t);
        desc.terms[0] = .{ .id = ecs.id(components.Food) };
        desc.terms[1] = .{ .id = ecs.id(components.Collidable) };
        const query = ecs.query_init(world, &desc) catch return stats;
        defer ecs.query_fini(query);
        var it = ecs.query_iter(world, query);
        while (ecs.query_next(&it)) {
            stats.food_on_ground += @intCast(it.count());
        }
    }

    // Count carried food (Food + CarriedBy)
    {
        var desc = std.mem.zeroes(ecs.query_desc_t);
        desc.terms[0] = .{ .id = ecs.id(components.Food) };
        desc.terms[1] = .{ .id = ecs.id(components.CarriedBy) };
        const query = ecs.query_init(world, &desc) catch return stats;
        defer ecs.query_fini(query);
        var it = ecs.query_iter(world, query);
        while (ecs.query_next(&it)) {
            stats.food_carried += @intCast(it.count());
        }
    }

    // Count ants with cooldown
    {
        var desc = std.mem.zeroes(ecs.query_desc_t);
        desc.terms[0] = .{ .id = ecs.id(components.Ant) };
        desc.terms[1] = .{ .id = ecs.id(components.Cooldown) };
        const query = ecs.query_init(world, &desc) catch return stats;
        defer ecs.query_fini(query);
        var it = ecs.query_iter(world, query);
        while (ecs.query_next(&it)) {
            stats.ants_with_cooldown += @intCast(it.count());
        }
    }

    return stats;
}
