# An Gatherers - Browser Implementation

This is a JavaScript/HTML5 Canvas implementation of the An Gatherers simulation, transpiled from the SudoLang specification.

## What You'll See

- **Gray squares**: Agents (ants) that move around the screen
- **Red squares**: Food resources scattered randomly
- **Emergent behavior**: Food naturally clusters into piles without explicit programming

## How It Works

The simulation demonstrates emergent collective behavior through simple rules:

1. **Agents move forward** in straight lines at constant speed
2. **When an agent encounters food**:
   - If not carrying anything: pick it up and turn around (~180°)
   - If already carrying food: drop the carried food and turn around
3. **Boundary wrapping**: Agents wrap around screen edges
4. **Cooldown period**: Brief pause after each food interaction

## Controls

- **Reset**: Restart the simulation with new random positions
- **Pause/Resume**: Stop/start the simulation
- The simulation runs automatically when loaded

## Files

- `index.html`: Main HTML page with canvas and controls
- `gatherers.js`: Complete JavaScript implementation
- `README.md`: This documentation

## Running Locally

Simply open `index.html` in any modern web browser. No server required.

## Technical Details

- **Canvas size**: 800x600 pixels
- **Agents**: 16 agents spawned in a horizontal line
- **Resources**: 80 food items placed randomly
- **Frame rate**: 60 FPS using `requestAnimationFrame`
- **Coordinate system**: Centered origin (-400 to +400 on both axes)

## Emergent Properties

Watch for these emergent behaviors:
- Food clustering into piles over time
- Self-organization without central control
- Collective intelligence emerging from individual actions
- System robustness despite random individual behaviors

This demonstrates how complex global patterns can emerge from simple local rules - a fundamental principle in complexity science and artificial life.
