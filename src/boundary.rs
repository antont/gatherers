//! Boundary wrapping system for entities that should wrap around screen edges
//! Originally from bevydroids

use bevy::{prelude::*, window::PrimaryWindow};

pub struct BoundaryPlugin;

impl Plugin for BoundaryPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(PostUpdate, boundary_wrap_system);
    }
}

#[derive(Debug, Component, Default, Clone, Copy, Deref, DerefMut)]
pub struct Bounding(f32);

impl Bounding {
    pub fn from_radius(radius: f32) -> Self {
        Self(radius)
    }
}

#[derive(Debug, Component, Default)]
pub struct BoundaryWrap;

fn boundary_wrap_system(
    primary_window: Query<&Window, With<PrimaryWindow>>,
    mut query: Query<(&mut Transform, &Bounding), With<BoundaryWrap>>,
) {
    let Ok(window) = primary_window.get_single() else {
        return;
    };

    let half_width = window.width() / 2.0;
    let half_height = window.height() / 2.0;

    for (mut transform, bounding) in query.iter_mut() {
        let radius = **bounding;
        let pos = &mut transform.translation;

        // Wrap horizontally when entity goes completely off screen
        if pos.x + radius < -half_width {
            pos.x = half_width + radius;
        } else if pos.x - radius > half_width {
            pos.x = -half_width - radius;
        }

        // Wrap vertically when entity goes completely off screen
        if pos.y + radius < -half_height {
            pos.y = half_height + radius;
        } else if pos.y - radius > half_height {
            pos.y = -half_height - radius;
        }
    }
}
