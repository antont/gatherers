/* Extended from https://github.com/bevyengine/bevy/blob/latest/examples/ecs/observers.rs#L196 */

use bevy::prelude::*;
use std::collections::{HashMap, HashSet};

#[derive(Resource, Default)]
pub struct SpatialIndex {
    map: HashMap<(i32, i32), HashSet<Entity>>,
}

/// Cell size has to be bigger than any `TriggerMine::radius`
const CELL_SIZE: f32 = 20.0;

impl SpatialIndex {
    // Lookup all entities within adjacent cells of our spatial index
    pub fn get_nearby(&self, pos: Vec2) -> Vec<Entity> {
        let tile = (
            (pos.x / CELL_SIZE).floor() as i32,
            (pos.y / CELL_SIZE).floor() as i32,
        );
        let mut nearby = Vec::new();
        for x in -1..2 {
            for y in -1..2 {
                if let Some(mines) = self.map.get(&(tile.0 + x, tile.1 + y)) {
                    nearby.extend(mines.iter());
                }
            }
        }
        nearby
    }

    pub fn update(&mut self, entity: Entity, pos: Vec2) {
        let tile = (
            (pos.x / CELL_SIZE).floor() as i32,
            (pos.y / CELL_SIZE).floor() as i32,
        );
        self.map.entry(tile).or_default().insert(entity);
    }

    pub fn remove(&mut self, entity: Entity) {
        for (_, mines) in self.map.iter_mut() {
            mines.remove(&entity);
        }
    }
}
