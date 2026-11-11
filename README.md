Sector Nine
===========

A vertically scrolling shooter for Linux in the 16-bit tradition.  You
fly one craft with two shapes: a tank that drives on the ground and a
ship that flies above it.  Walls stop the tank and energy roofs stop
the ship.  The ship's guns reach aircraft and the gun towers that stand
on raised blocks.  The tank's gun reaches the vehicles on the ground.
You switch back and forth to get through a sector.

All graphics and sound effects are generated in code for now.  They
are placeholders.

Building
--------

Requires a C compiler, make, pkg-config, SDL2 and SDL2_mixer:

    sudo apt install build-essential pkg-config libsdl2-dev libsdl2-mixer-dev
    make
    ./sector9

Controls
--------

The game is made for a one-button joystick: hold fire to shoot, wiggle
the stick quickly (left-right-left or up-down-up) to morph between tank
and ship.  Space does the same and is easier on a keyboard.

| Key              | Action                      |
|------------------|-----------------------------|
| Arrows or WASD   | Move                        |
| Z or Ctrl        | Fire                        |
| Space or X       | Morph                       |
| P                | Pause                       |
| F11              | Toggle fullscreen           |
| Esc              | Quit                        |

A game controller also works: stick or d-pad to move, A to fire, B or
a wiggle to morph.

The tank sets its own pace: the screen scrolls only when you drive
forward.  It faces and fires the way it drives, in eight directions.  The ship
flies on by itself at a steady pace and halts in front of a roof.  The
screen never scrolls back.

Ground and air are two separate levels.  Shots from ground vehicles
only hit the tank, and stop at walls.  Shots from aircraft and towers
only hit the ship.  When the sky gets crowded you can drop to the ground
and let it pass overhead, and the other way around.  Shots that cannot
hit your current shape are drawn faint.

Pickups
-------

Destroyed enemies sometimes leave a capsule.  A wrecked tower may have
one rise out of its block, where only the ship can reach it.

| Capsule | Effect                                        |
|---------|-----------------------------------------------|
| G       | Gun upgrade, three levels                     |
| S       | Shield, restores two points                   |
| F       | Faster craft, two levels                      |
| C       | Companion that follows you and fires with you |
| H       | Ten homing missiles, fired along with the gun |
| B       | Smart bomb, goes off when you pick it up      |

Missiles and the smart bomb ignore the two levels: they hit anything on
screen, on the ground or in the air.  The bomb also wipes enemy shots.

Losing a life costs one gun level, one companion and all speed.

Each sector ends with a fortress.  Its core sits on the ground and is
shielded until both of its towers are shot down from the air.

Music
-----

Tracker modules are loaded from the `music` directory next to the
binary, in any format SDL2_mixer can play:

| File               | Played           |
|--------------------|------------------|
| `music/title.mod`  | On the title screen |
| `music/sector.mod` | While playing    |

A missing file means silence.  The title tune is "Enigma" by Phenomena,
from [The Mod Archive](https://modarchive.org/).
