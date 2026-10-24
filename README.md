# lcplates-changer

An in-game plate changer for **[LibertyCityPlates](https://github.com/ImpossibleEchoes/LibertyCityPlates)** in GTA IV. Press **F11** in a car, or after getting out of one, type a new plate, and it's on the car straight away.

It's a small `.asi` plugin. It works on Complete Edition 1.2.0.59 with [FusionFix](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix), and should also work on 1.0.x/1.1.x.

## Install

1. Download `PlateChanger.asi` and `PlateChanger.ini` from the [latest release](../../releases/latest) (or from the newest build under **Actions**).
2. Put both next to `LibertyCityPlates.asi` in your game folder.

You need LibertyCityPlates (it provides the plate commands this plugin uses), an ASI loader (FusionFix already has one), and windowed or borderless mode so the box can appear over the game.

## Use

Press **F11**. The game pauses and a small **Change plate** box opens:

| Field | What it does |
|---|---|
| Plate text | Up to 12 characters: `A`–`Z`, `0`–`9`, spaces. `*` is the state emblem. |
| Plate picture # | Optional. The `plateN` texture number in `libertycityplates.wtd`. Blank keeps the current one. |
| Font # | Optional. The `lettersN` font number. Blank keeps the current one. |

**Enter** applies, **Esc** cancels. A message at the bottom of the screen confirms the change.

The new plate stays until the car leaves the world (it streams out, you reload, or it's re-created from a garage). After that LibertyCityPlates picks a plate from `vehicles.ide` again, as usual.

## Settings

`PlateChanger.ini`:

```ini
[PlateChanger]
Key=F11
```

`Key` accepts `F1`–`F12`, a single letter or digit, and optional `Ctrl+`, `Alt+`, `Shift+` (e.g. `Shift+F11`). Liberty's Legacy trainer also uses F11, so change it if both react.

## Troubleshooting

The plugin writes `PlateChanger.log` next to itself. It records whether it found the game's script command table, the game window and LibertyCityPlates. Open an issue with that log if the hotkey does nothing.

## How it works

- It finds the game's script command table with the same byte patterns FusionFix uses for 1.0.x and 1.2.x, and calls commands by hash.
- It hooks the game window's message handler and checks the hotkey there, on the game's main thread between frames.
- It calls LibertyCityPlates' own script commands (`SET_CAR_LICENSE_PLATE_TEXT_0123` = `0x09DED943` and friends, from its source) to set the text, length, plate picture and font.
- It's plain Win32 with no C runtime, so it depends only on `kernel32`, `user32` and `gdi32`.

## Building

```
pip install ziglang
./build.sh
```

This produces `build/PlateChanger.asi`. Any 32-bit Windows C compiler works too. Build `src/platechanger.c` as a DLL and rename it to `.asi`.

## Credits

- **LibertyCityPlates** by Shvab, CP and Ash_735, for the plate system and its script commands.
- **FusionFix** by ThirteenAG and contributors, whose source documents the command table patterns this plugin looks for.

## License

[MIT](LICENSE)
