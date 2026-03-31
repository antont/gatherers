const std = @import("std");
const ecs = @import("zflecs");

pub fn main() !void {
    const world = ecs.init();
    defer _ = ecs.fini(world);

    _ = ecs.progress(world, 0);

    std.debug.print("zig-gatherers: flecs world initialized and ticked successfully\n", .{});
}
