# !Scale v0.7.0

**Reblebro4200**

Launch **!Scale** from Steam or run `./Play-'!Scale'.sh`. Keep `assets/`
and `world/` beside the executable. The native build runs on x86-64
Linux with SDL2 and OpenGL 3.3.

## Worlds

v0.7.0 keeps the existing campaign library. Create
another world to enter an unsigned 64-bit seed, or leave the field blank
for a random seed. Press Tab to switch between the name and seed fields.
The pause menu shows the active seed. The same seed recreates terrain
outside the starter route and the variation inside hidden realms.
Inventory, terrain edits, and progression are separate for every world.

Use `--save PATH` to open a copy of a save in v0.7.0. The launcher
backs up the older save beside it before writing the new format.
`--no-save` starts a temporary session.

## Build from source

Run `./build.sh` on x86-64 Linux with a C++20 compiler and SDL2 installed.
Extract the `world/` directory from the Linux release ZIP beside the source
build. To generate replacement terrain locally, run these commands from the
source root.

```sh
mkdir -p world
cc -O2 src/worldgen.c -lm -o /tmp/scale-worldgen
/tmp/scale-worldgen
```

Run `./scale-frontier --check-install` to check the bundle and
`./scale-frontier --self-test` for the game model tests. On Windows, run
`Build-Windows.ps1 -LinuxZip PATH_TO_LINUX_ZIP` from an x64 Visual Studio
Native Tools command prompt. The Linux ZIP supplies the shared assets and terrain.

## Default controls

| Action | Input |
| --- | --- |
| Move and look | WASD and mouse |
| Sprint, crouch, jump | Ctrl, Shift, Space |
| Toggle flight | V or Right Alt + Space |
| Shrink or grow | Mouse wheel or + / − |
| Mine or fire | Left mouse button |
| Interact, place, or aim | Right mouse button |
| Interact or place with a weapon equipped | Shift + right mouse button |
| Flashlight, exact block mining | F, I |
| Roll for a random item | G |
| Inventory, build materials, workbench | Tab, H, C |
| Cycle equipment, inspect integrated rifle sight | X, Z |
| Return to the parent realm | B |
| Render resolution, mouse capture, fullscreen | F7, F8, F11 |
| Live FPS | F3 |

Rebind gameplay actions in **Esc → Settings → Keybinds**. The inventory
shows the items you currently carry. The HUD shows one selected piece
and its count. The reward roll has a short cooldown and draws from
the game’s inventory items, including weapons and machine parts.

SDL-compatible controllers and Steam Input gamepads use left stick to
move, right stick to look, triggers to aim and mine or fire, and bumpers
to grow and shrink. A jumps, X interacts or places, Y opens inventory,
and Start pauses. D-pad Down toggles the flashlight. D-pad Up cycles
weapons, and Left/Right selects the previous/next owned building piece.
Use the directional pad, A, and B in menus.

## Terrain at small scales

Each wheel detent changes one size tier. Delbert is 1.5 blocks tall
at every tier while standing. Sprint speed scales with his size. If
solid terrain blocks the next larger tier, move clear and try again.

Loose crystals, rocks, and ores have faceted surfaces. Outdoor ground
and the natural parts of hidden areas use connected slopes. With your
weapon holstered, left-click any mineable terrain material to scoop a
hollow. Select a material and right-click to build a mound. Surface ores
require a pick, and camp flooring requires an iron pick.
Each stroke transfers the volume you actually move. Overlap strokes
to dig a trench or build a ramp. Shrinking gives you a smaller shovel.

Sculpting works in the atomic realm and at the finest surface tier.
For grid construction, use I to mine a block and Shift + right-click
with a holstered weapon to place a block at the selected scale.
The deepest tiers use the reticle and material name to indicate the
selected piece.

## Expedition

You begin beside North with a rifle and six rounds. Find Reedwatch
south-east of the camp and visit the hidden realms to find smaller
goblin settlements. Each settlement has one chest. Its guard runs
to protect it as you approach. Defeat the guard or draw it away from
the chest to open it. The Buried Goblin Hold is in the deepest realm.

The cracked planet is clearest at night. god. circles while you watch and quickens its approach when you look away. It can climb walls, drop from ledges, and crawl around obstacles beneath low ceilings.

## Workshop

Interact with the camp crafting table or press C in camp to craft tools
and components. Use the separate Machine Crafter near the south trail
for machines. Craft and place crank generators or solar panels as needed.
Solar provides twice the crank's power on the same circuit in daylight
and stops producing at night. Connect devices with adjacent cable.

The Prism Press makes weapons and rounds. The rifle includes a permanent optic. Carry a pistol or rifle
to enter a portal. An iron pick recovers gates and camp flooring. The
Resource Generator costs five iron and produces one selected block per
second. The House Builder needs 25 mW and makes furnishings.

## Troubleshooting

- `./Play-'!Scale'.sh --check-install` verifies bundled assets.
- `./Play-'!Scale'.sh --benchmark --render-scale 55` measures four scenes
  in a hidden 1280 × 720 window.
- `./Play-'!Scale'.sh --windowed --software` provides a graphics fallback.
  Launch errors are logged under `~/.local/state/scale-frontier/` or
  the equivalent directory under `XDG_STATE_HOME`.

The glyph textures use DejaVu Sans Mono and DejaVu Serif. Font licenses
are in `assets/`.
