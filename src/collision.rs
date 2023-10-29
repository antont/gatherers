use std::marker::PhantomData;

use bevy::{prelude::*, ecs::schedule::ScheduleLabel};

use crate::boundary::Bounding;

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
        app.add_event::<HitEvent<Hittable, Hitter>>()
            .add_systems(Update, collision_system::<Hittable, Hitter>.in_set(CollisionSystemLabel));
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
    hittables: Query<(Entity, &Transform, &Bounding), (With<Collidable>, With<A>)>,
    hitters: Query<(Entity, &Transform, &Bounding), (With<Collidable>, With<B>)>,
) {
    for (hittable_entity, hittable_transform, hittable_bounds) in hittables.iter() {
        for (hitter_entity, hitter_transform, hitter_bounds) in hitters.iter() {
            let distance = (hittable_transform.translation - hitter_transform.translation).length();
            //print!("[collision_system] Distance: {}", distance);
            if distance < **hittable_bounds + **hitter_bounds {
                //print!("[collision_system] Hit: {}", distance);
                hits.send(HitEvent {
                    entities: (hittable_entity, hitter_entity),
                    _phantom: PhantomData,
                });
            }
        }
    }
}
