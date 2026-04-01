const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const zflecs = b.dependency("zflecs", .{});

    // Main executable
    const exe_mod = b.createModule(.{
        .root_source_file = b.path("src/main.zig"),
        .target = target,
        .optimize = optimize,
    });
    exe_mod.addImport("zflecs", zflecs.module("root"));

    const exe = b.addExecutable(.{
        .name = "zig-gatherers",
        .root_module = exe_mod,
    });
    exe.linkLibrary(zflecs.artifact("flecs"));
    b.installArtifact(exe);

    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());
    const run_step = b.step("run", "Run the simulation");
    run_step.dependOn(&run_cmd.step);

    // Tests
    const test_step = b.step("test", "Run unit tests");

    const test_files = [_][]const u8{
        "src/config.zig",
        "src/spatial_index.zig",
        "src/spawn.zig",
        "src/systems.zig",
        "src/main.zig",
    };

    for (test_files) |file| {
        const t_mod = b.createModule(.{
            .root_source_file = b.path(file),
            .target = target,
            .optimize = optimize,
        });
        t_mod.addImport("zflecs", zflecs.module("root"));

        const t = b.addTest(.{
            .root_module = t_mod,
        });
        t.linkLibrary(zflecs.artifact("flecs"));
        const run_t = b.addRunArtifact(t);
        test_step.dependOn(&run_t.step);
    }
}
