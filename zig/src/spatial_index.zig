const std = @import("std");

// SpatialIndex — to be implemented

// =============================================================================
// Tests
// =============================================================================

const testing = std.testing;

test "insert and getNearby returns entity in same cell" {
    const SpatialIndex = @import("spatial_index.zig").SpatialIndex;
    var si = SpatialIndex.init(testing.allocator);
    defer si.deinit();

    try si.insert(42, 10.0, 10.0);
    const nearby = try si.getNearby(10.0, 10.0);
    defer testing.allocator.free(nearby);

    try testing.expectEqual(@as(usize, 1), nearby.len);
    try testing.expectEqual(@as(u64, 42), nearby[0]);
}

test "getNearby returns entities in adjacent cells (3x3 neighborhood)" {
    const SpatialIndex = @import("spatial_index.zig").SpatialIndex;
    var si = SpatialIndex.init(testing.allocator);
    defer si.deinit();

    // cell_size = 20. Entity at (5, 5) is in cell (0, 0).
    // Query at (25, 25) is in cell (1, 1) — cell (0,0) is in 3x3 neighborhood.
    try si.insert(1, 5.0, 5.0);
    const nearby = try si.getNearby(25.0, 25.0);
    defer testing.allocator.free(nearby);

    try testing.expectEqual(@as(usize, 1), nearby.len);
    try testing.expectEqual(@as(u64, 1), nearby[0]);
}

test "getNearby does NOT return entities outside 3x3 neighborhood" {
    const SpatialIndex = @import("spatial_index.zig").SpatialIndex;
    var si = SpatialIndex.init(testing.allocator);
    defer si.deinit();

    // Entity at (5, 5) is in cell (0, 0).
    // Query at (65, 65) is in cell (3, 3) — cell (0,0) is NOT in 3x3 neighborhood.
    try si.insert(1, 5.0, 5.0);
    const nearby = try si.getNearby(65.0, 65.0);
    defer testing.allocator.free(nearby);

    try testing.expectEqual(@as(usize, 0), nearby.len);
}

test "remove entity from spatial index" {
    const SpatialIndex = @import("spatial_index.zig").SpatialIndex;
    var si = SpatialIndex.init(testing.allocator);
    defer si.deinit();

    try si.insert(42, 10.0, 10.0);
    si.remove(42);
    const nearby = try si.getNearby(10.0, 10.0);
    defer testing.allocator.free(nearby);

    try testing.expectEqual(@as(usize, 0), nearby.len);
}

test "clear removes all entities" {
    const SpatialIndex = @import("spatial_index.zig").SpatialIndex;
    var si = SpatialIndex.init(testing.allocator);
    defer si.deinit();

    try si.insert(1, 10.0, 10.0);
    try si.insert(2, 50.0, 50.0);
    try si.insert(3, -100.0, 200.0);
    si.clear();

    const nearby = try si.getNearby(10.0, 10.0);
    defer testing.allocator.free(nearby);
    try testing.expectEqual(@as(usize, 0), nearby.len);
}

test "multiple entities in same cell" {
    const SpatialIndex = @import("spatial_index.zig").SpatialIndex;
    var si = SpatialIndex.init(testing.allocator);
    defer si.deinit();

    try si.insert(1, 5.0, 5.0);
    try si.insert(2, 15.0, 15.0); // same cell (0, 0) since cell_size=20
    const nearby = try si.getNearby(10.0, 10.0);
    defer testing.allocator.free(nearby);

    try testing.expectEqual(@as(usize, 2), nearby.len);
}

test "negative coordinates hash correctly" {
    const SpatialIndex = @import("spatial_index.zig").SpatialIndex;
    var si = SpatialIndex.init(testing.allocator);
    defer si.deinit();

    try si.insert(1, -15.0, -15.0); // cell (-1, -1)
    const nearby = try si.getNearby(-10.0, -10.0); // cell (-1, -1)
    defer testing.allocator.free(nearby);

    try testing.expectEqual(@as(usize, 1), nearby.len);
    try testing.expectEqual(@as(u64, 1), nearby[0]);
}
