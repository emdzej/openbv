# Controls

The defaults of Babo Violent 2 2.11 (`GameVar.cpp`); change them in Options. The keys are physical
keys: on any layout, W is the key where W is on a US keyboard.

| Action | Key |
|---|---|
| Move up / down / left / right | W / S / A / D |
| Aim | the mouse |
| Shoot (and respawn) | left mouse button |
| Throw a grenade | right mouse button |
| Throw a molotov cocktail | middle mouse button |
| Melee | Space |
| Pick up a weapon | F |
| Chat with everyone | T |
| Chat with your team | Y |
| Scores | Tab |
| Menu | Escape |
| Statistics | L |
| Screenshot | P |
| Console | ` (backquote) |

A screenshot (P) is saved as a BMP in gasm's storage, next to `bv2.cfg`.

Holding Escape for a moment quits gasm-run; a tap goes to the game.

## Gamepads

The original read an Xbox 360 controller through DirectInput when `cl_enableXBox360Controller` was
on. openbv maps the first gamepad's sticks and triggers the same way; its buttons aren't mapped yet
(see [Status](/status)).
