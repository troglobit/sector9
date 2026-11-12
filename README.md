Sector Nine
===========

A vertically scrolling shooter for Linux in the 16-bit tradition.  You
fly one craft with two shapes: a tank that drives on the ground and a
ship that flies above it.  Walls stop the tank and energy roofs stop
the ship.  The ship's guns reach aircraft and the gun towers that stand
on raised blocks.  The tank's gun reaches the vehicles on the ground.
You switch back and forth to get through a sector.

All graphics and sound effects are generated in code.  Sprites, tiles
and text are drawn from shapes at the resolution of the window, so the
picture stays sharp at any size.

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
| M                | Mouse steering: marker or direct |
| - and +          | Mouse speed                 |
| F11              | Toggle fullscreen           |
| Esc              | Back to the title screen, quit from there |

A game controller also works: stick or d-pad to move, A to fire, B or
a wiggle to morph.

So does a mouse.  Click in the window to capture the pointer; pausing
lets it go again.  The mouse moves a small marker and your craft heads
for it: the ship in a straight line, the tank in the nearest of its
eight directions.  The craft stops when it gets there, so park the
marker where you want to be, or leave it at the top of the screen to
keep driving.  Hold the left button to fire and press the right one to
morph.

If you would rather have the craft move as the mouse does and stop
when it stops, press M for direct steering; M again brings the marker
back.  The minus and plus keys change the mouse speed.  Both settings
are saved.  `sector9 --mouse direct` (or `marker`) sets the steering
from the command line, and `sector9 --help` lists all options.

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

There are nine sectors.  The first ones are short and leave out the
harder enemies.  Later sectors are longer, bring in gunships and heavy
tanks, and everything in them fires faster.  Every 20,000 points earns
an extra life.

The high score is saved in `~/.local/share/sector9/hiscore` when you
quit.

Sector nine is the last one, and it looks the part.  Its fortress has
four towers, and when the core has taken half of its damage it lifts
off the ground, so the tank starts that fight and the ship has to
finish it.

Beyond sector nine
------------------

Clearing sector nine fuses the tank and the ship into one craft.  It
flies, it cannot morph, and it is on both levels at once: its guns hit
everything, and every shot can hit it.  What follows is open space
with islands of deck drifting past, and it goes on until you run out
of lives.  Each lap scrolls faster and multiplies the score: twice on
the first lap, three times on the second, and so on.

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

License
-------

The code, and the graphics and sound effects it generates, are under
the [ISC license](LICENSE).  The tracker modules in `music` are not
part of that; each one belongs to its composer.
