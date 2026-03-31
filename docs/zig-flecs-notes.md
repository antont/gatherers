# Zig/flecs Port Notes

## Carrying Model: Carrying/CarriedBy vs flecs ChildOf

The Rust/Bevy version uses Bevy's parent-child relationships (`add_child` / `ChildOf`) for carrying. Bevy's transform propagation automatically moves carried food with the ant — the food's local Transform is `(0, 0, z)` relative to its parent.

The Zig/flecs port uses explicit `Carrying { food }` on the ant and `CarriedBy { ant }` on the food instead of flecs `ChildOf` pairs. The movement system manually syncs carried food position after each ant move.

### Why not flecs ChildOf?

Flecs does support `ChildOf` relationship pairs:

```zig
ecs.add_pair(world, food, EcsChildOf, ant);   // attach
ecs.remove_pair(world, food, EcsChildOf, ant); // detach (child survives)
```

This would be idiomatic flecs, but:

1. **No auto transform propagation** — flecs has no built-in Position hierarchy like Bevy's `GlobalTransform`. We'd still need manual position sync in a headless sim.
2. **Cascading delete risk** — deleting a parent deletes all `ChildOf` children. If an ant were ever destroyed, its carried food would vanish. The explicit component approach avoids this.
3. **O(1) bidirectional lookup** — `Carrying` on ant and `CarriedBy` on food give instant access in both directions without relationship queries.

### When to revisit

If a rendering layer is added (e.g. via flecs REST explorer, or a graphics backend that understands flecs hierarchies), switching to `ChildOf` pairs could make the hierarchy visible to those tools. The `Carrying`/`CarriedBy` components would then become redundant.
