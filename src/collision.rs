use std::marker::PhantomData;
use bevy::{prelude::*, ecs::schedule::ScheduleLabel};
use crate::boundary::Bounding;
use crate::spatial_index::SpatialIndex;

pub struct CollisionPlugin<Hittable, Hitter> {
    _phantom: PhantomData<(Hittable, Hitter)>,
}

impl<Hittable: Component, Hitter: Component> CollisionPlugin<Hittable, Hitter> {
    pub fn new() -> Self {
        Self {
            _phantom: PhantomData,
        }
    }
}

impl<Hittable: Component, Hitter: Component> Plugin for CollisionPlugin<Hittable, Hitter> {
    fn build(&self, app: &mut App) {
        app.init_resource::<SpatialIndex>()
            .add_event::<HitEvent<Hittable, Hitter>>()
            .add_systems(Startup, initialize_hittables::<Hittable>)
            .add_systems(Update, update_hittable_positions::<Hittable>)
            .add_systems(Update, update_hitters_spatial_index::<Hitter>)
            .add_systems(Update, collision_system::<Hittable, Hitter>);
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, SystemSet, ScheduleLabel)]
pub struct CollisionSystemLabel;

#[derive(Debug, Event)]
pub struct HitEvent<A, B> {
    entities: (Entity, Entity),
    _phantom: PhantomData<(A, B)>,
}

impl<A, B> HitEvent<A, B> {
    pub fn hittable(&self) -> Entity {
        self.entities.0
    }

    pub fn hitter(&self) -> Entity {
        self.entities.1
    }
}

#[derive(Debug, Component)]
pub struct Collidable;

fn collision_system<A: Component, B: Component>(
    mut hits: EventWriter<HitEvent<A, B>>,
    spatial_index: Res<SpatialIndex>,
    hittables: Query<(Entity, &Transform, &Bounding), (With<Collidable>, With<A>)>,
    hitters: Query<(Entity, &Transform, &Bounding), (With<Collidable>, With<B>)>,
) {
    for (hitter_entity, hitter_transform, hitter_bounds) in hitters.iter() {
        let nearby_entities = spatial_index.get_nearby(hitter_transform.translation.truncate());
        
        for &nearby_entity in nearby_entities.iter() {
            if let Ok((hittable_entity, hittable_transform, hittable_bounds)) = hittables.get(nearby_entity) {
                let distance = (hittable_transform.translation - hitter_transform.translation).length();
                if distance < **hittable_bounds + **hitter_bounds {
                    hits.send(HitEvent {
                        entities: (hittable_entity, hitter_entity),
                        _phantom: PhantomData,
                    });
                }
            }
        }
    }
}

fn initialize_hittables<Hittable: Component>(
    mut spatial_index: ResMut<SpatialIndex>,
    query: Query<(Entity, &Transform), With<Hittable>>,
) {
    for (entity, transform) in query.iter() {
        spatial_index.update_spatial_index(entity, transform.translation.truncate());
    }
}

fn update_hitters_spatial_index<Hitter: Component>(
    mut spatial_index: ResMut<SpatialIndex>,
    query: Query<(Entity, &Transform), With<Hitter>>,
) {
    for (entity, transform) in query.iter() {
        spatial_index.update_spatial_index(entity, transform.translation.truncate());
    }
}

// This system updates hittables that have been dropped or picked up
fn update_hittable_positions<Hittable: Component>(
    mut spatial_index: ResMut<SpatialIndex>,
    added_query: Query<(Entity, &Transform), (With<Hittable>, Added<Collidable>)>,
    //mut removed: RemovedComponents<Collidable>
) {
    // Update positions for newly dropped food
    for (entity, transform) in added_query.iter() {
        spatial_index.update_spatial_index(entity, transform.translation.truncate());
    }

    //Remove entries for newly picked up food
    // for entity in removed.read() {
    //     spatial_index.remove(entity);
    // }
}
