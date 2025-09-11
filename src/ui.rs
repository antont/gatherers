//! UI system for the gatherers simulation
//! Handles speed control via keyboard and other UI elements

use crate::config::{Config, SimulationSettings};
use bevy::prelude::*;

pub struct UiPlugin;

impl Plugin for UiPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Startup, setup_ui)
            .add_systems(Update, handle_keyboard_input);
    }
}

#[derive(Component)]
pub struct SpeedLabel;

#[derive(Component)]
pub struct SpeedBar;

pub fn setup_ui(mut commands: Commands) {
    // Root UI container
    commands
        .spawn(Node {
            position_type: PositionType::Absolute,
            top: Val::Px(10.0),
            left: Val::Px(10.0),
            width: Val::Px(350.0),
            height: Val::Px(120.0),
            flex_direction: FlexDirection::Column,
            justify_content: JustifyContent::SpaceBetween,
            padding: UiRect::all(Val::Px(15.0)),
            ..default()
        })
        .insert(BackgroundColor(Color::srgba(0.0, 0.0, 0.0, 0.8)))
        .with_children(|parent| {
            // Speed label
            parent.spawn((
                SpeedLabel,
                Text::new("Speed: 1.0x"),
                TextFont {
                    font_size: 18.0,
                    ..default()
                },
                TextColor(Color::WHITE),
            ));

            // Speed indicator bar container
            parent
                .spawn(Node {
                    width: Val::Px(300.0),
                    height: Val::Px(20.0),
                    border: UiRect::all(Val::Px(2.0)),
                    ..default()
                })
                .insert(BackgroundColor(Color::srgb(0.3, 0.3, 0.3)))
                .insert(BorderColor(Color::WHITE))
                .with_children(|parent| {
                    // Speed bar fill (shows current speed level)
                    parent.spawn((
                        SpeedBar,
                        Node {
                            width: Val::Percent(10.0), // Start at 1.0x (10% of max 10.0x)
                            height: Val::Percent(100.0),
                            ..default()
                        },
                        BackgroundColor(Color::srgb(0.2, 0.8, 0.2)), // Green bar
                    ));
                });

            // Instructions
            parent.spawn((
                Text::new("Controls:"),
                TextFont {
                    font_size: 14.0,
                    ..default()
                },
                TextColor(Color::srgb(0.8, 0.8, 0.8)),
            ));

            parent.spawn((
                Text::new("[-/=] Decrease/Increase Speed"),
                TextFont {
                    font_size: 14.0,
                    ..default()
                },
                TextColor(Color::srgb(0.8, 0.8, 0.8)),
            ));

            parent.spawn((
                Text::new("[R] Reset to 1.0x"),
                TextFont {
                    font_size: 14.0,
                    ..default()
                },
                TextColor(Color::srgb(0.8, 0.8, 0.8)),
            ));
        });
}

fn handle_keyboard_input(
    keyboard_input: Res<ButtonInput<KeyCode>>,
    mut settings: ResMut<SimulationSettings>,
    mut label_query: Query<&mut Text, With<SpeedLabel>>,
    mut bar_query: Query<&mut Node, With<SpeedBar>>,
) {
    let mut speed_changed = false;
    let mut new_speed = settings.speed_multiplier;

    // Decrease speed with - (minus) key
    if keyboard_input.just_pressed(KeyCode::Minus) {
        new_speed = (new_speed - 0.5).max(Config::MIN_SPEED_MULTIPLIER);
        speed_changed = true;
    }

    // Increase speed with = (equals) key
    if keyboard_input.just_pressed(KeyCode::Equal) {
        new_speed = (new_speed + 0.5).min(Config::MAX_SPEED_MULTIPLIER);
        speed_changed = true;
    }

    // Reset to 1.0x with R key
    if keyboard_input.just_pressed(KeyCode::KeyR) {
        new_speed = 1.0;
        speed_changed = true;
    }

    // Update settings and UI if speed changed
    if speed_changed {
        settings.speed_multiplier = new_speed;

        // Update label
        if let Ok(mut text) = label_query.single_mut() {
            text.0 = format!("Speed: {:.1}x", new_speed);
        }

        // Update visual speed bar
        if let Ok(mut bar_node) = bar_query.single_mut() {
            let speed_percent = ((new_speed - Config::MIN_SPEED_MULTIPLIER)
                / (Config::MAX_SPEED_MULTIPLIER - Config::MIN_SPEED_MULTIPLIER))
                * 100.0;
            bar_node.width = Val::Percent(speed_percent);
        }
    }
}
