# gatherers
Simple Gatherers simulation in Rust, using Bevy. Port of the Breve Gatherers original.

- Live on the web: https://an.org/gatherers/

The white boxes, 'ants', follow most simple rules, and *don't know anything about each other*:
- go straight in random direction
- if encounter food, pick it up, and turn back to a random direction
- if encounter food, but is already carrying one, drop the food, and turn back

This results in the food getting gathered in piles, maybe eventually in a single pile, even though the gatherers don't know anything about each other, nor have such a goal really written explicitly. This is a simple emergent phenomenon.

Is a loose port of the original Gatherers demo made in Breve, back in 2002 or so.
The original code is in: https://github.com/jonklein/breve/blob/master/demos/Gatherers.tz

Made using the Bevy game engine for Rust, reusing the wrap-around system and other lessons from the Asteroids clone Bevydroids. Thanks!

# TODO:
- [x] Basic behaviour from the original Breve Gatherers working
- [ ] UI Speed control slider:
  * need to set the speed to a var & the pick-up cooldown to be calculated from the speed sensibly
- [ ] Respond to windows size change in the browser, and make full browser window sized.
  * Works on the desktop already, just needs the browser hooks
- [ ] Optimize and improve collision detection
  * maybe using continuous collision detection which would be a nice fit with the linear movements here
  * perhaps use https://github.com/SergiusIW/collider-rs or https://github.com/patrik-cihal/perfect-collisions
