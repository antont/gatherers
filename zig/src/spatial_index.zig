const std = @import("std");
const config = @import("config.zig");

const CellKey = struct {
    x: i32,
    y: i32,
};

const EntityList = std.ArrayList(u64);

pub const SpatialIndex = struct {
    map: std.AutoHashMap(CellKey, EntityList),
    allocator: std.mem.Allocator,

    pub fn init(allocator: std.mem.Allocator) SpatialIndex {
        return .{
            .map = std.AutoHashMap(CellKey, EntityList).init(allocator),
            .allocator = allocator,
        };
    }

    pub fn deinit(self: *SpatialIndex) void {
        var it = self.map.valueIterator();
        while (it.next()) |list| {
            list.deinit(self.allocator);
        }
        self.map.deinit();
    }

    fn toCell(x: f32, y: f32) CellKey {
        return .{
            .x = @intFromFloat(@floor(x / config.spatial_cell_size)),
            .y = @intFromFloat(@floor(y / config.spatial_cell_size)),
        };
    }

    pub fn insert(self: *SpatialIndex, entity: u64, x: f32, y: f32) !void {
        const cell = toCell(x, y);
        const result = try self.map.getOrPut(cell);
        if (!result.found_existing) {
            result.value_ptr.* = .{};
        }
        try result.value_ptr.append(self.allocator, entity);
    }

    pub fn remove(self: *SpatialIndex, entity: u64) void {
        var it = self.map.valueIterator();
        while (it.next()) |list| {
            var i: usize = 0;
            while (i < list.items.len) {
                if (list.items[i] == entity) {
                    _ = list.swapRemove(i);
                } else {
                    i += 1;
                }
            }
        }
    }

    pub fn clear(self: *SpatialIndex) void {
        var it = self.map.valueIterator();
        while (it.next()) |list| {
            list.clearRetainingCapacity();
        }
    }

    pub fn getNearby(self: *SpatialIndex, x: f32, y: f32) ![]u64 {
        const center = toCell(x, y);
        var result: EntityList = .{};

        const offsets = [_]i32{ -1, 0, 1 };
        for (offsets) |dx| {
            for (offsets) |dy| {
                const key = CellKey{ .x = center.x + dx, .y = center.y + dy };
                if (self.map.get(key)) |list| {
                    try result.appendSlice(self.allocator, list.items);
                }
            }
        }

        return result.toOwnedSlice(self.allocator);
    }
};

// =============================================================================
// Tests
// =============================================================================

const testing = std.testing;

test "insert and getNearby returns entity in same cell" {
    var si = SpatialIndex.init(testing.allocator);
    defer si.deinit();

    try si.insert(42, 10.0, 10.0);
    const nearby = try si.getNearby(10.0, 10.0);
    defer testing.allocator.free(nearby);

    try testing.expectEqual(@as(usize, 1), nearby.len);
    try testing.expectEqual(@as(u64, 42), nearby[0]);
}

test "getNearby returns entities in adjacent cells (3x3 neighborhood)" {
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
    var si = SpatialIndex.init(testing.allocator);
    defer si.deinit();

    try si.insert(42, 10.0, 10.0);
    si.remove(42);
    const nearby = try si.getNearby(10.0, 10.0);
    defer testing.allocator.free(nearby);

    try testing.expectEqual(@as(usize, 0), nearby.len);
}

test "clear removes all entities" {
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
    var si = SpatialIndex.init(testing.allocator);
    defer si.deinit();

    try si.insert(1, 5.0, 5.0);
    try si.insert(2, 15.0, 15.0); // same cell (0, 0) since cell_size=20
    const nearby = try si.getNearby(10.0, 10.0);
    defer testing.allocator.free(nearby);

    try testing.expectEqual(@as(usize, 2), nearby.len);
}

test "negative coordinates hash correctly" {
    var si = SpatialIndex.init(testing.allocator);
    defer si.deinit();

    try si.insert(1, -15.0, -15.0); // cell (-1, -1)
    const nearby = try si.getNearby(-10.0, -10.0); // cell (-1, -1)
    defer testing.allocator.free(nearby);

    try testing.expectEqual(@as(usize, 1), nearby.len);
    try testing.expectEqual(@as(u64, 1), nearby[0]);
}
