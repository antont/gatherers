const ecs = @import("zflecs");

// --- Tag components (zero-size) ---
pub const Ant = struct {};
pub const Food = struct {};
pub const Collidable = struct {};
pub const BoundaryWrap = struct {};

// --- Data components ---
pub const Position = struct {
    x: f32 = 0,
    y: f32 = 0,
};

pub const Velocity = struct {
    x: f32 = 0,
    y: f32 = 0,
};

pub const Bounding = struct {
    radius: f32 = 0,
};

pub const Cooldown = struct {
    timer: f32 = 0,
};

pub const Carrying = struct {
    food: ecs.entity_t = 0,
};

pub const CarriedBy = struct {
    ant: ecs.entity_t = 0,
};

pub fn registerAll(world: *ecs.world_t) void {
    // Tags
    ecs.TAG(world, Ant);
    ecs.TAG(world, Food);
    ecs.TAG(world, Collidable);
    ecs.TAG(world, BoundaryWrap);

    // Data components
    ecs.COMPONENT(world, Position);
    ecs.COMPONENT(world, Velocity);
    ecs.COMPONENT(world, Bounding);
    ecs.COMPONENT(world, Cooldown);
    ecs.COMPONENT(world, Carrying);
    ecs.COMPONENT(world, CarriedBy);
}
