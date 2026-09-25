# antika geode mod

A Geometry Dash mod for [Geode](https://geode-sdk.org) that adds a set of opt-in
gameplay toggles. Every setting is **off by default** and nothing changes until you
enable it yourself.

## Features

| Setting | What it does |
| --- | --- |
| Story Mode | Plays level `149188646` on the main menu and shows an ending dialog once you beat it. |
| Negative Hitboxes | Inverts every object's scale and turns off the level's *Fix Negative Scale* option, so hazards are safe and empty space is deadly. |
| Negative | Upside-down world plus a photo-negative colour filter, on top of Negative Hitboxes. Requires Negative Hitboxes. |
| Accurate Hitboxes | Spikes and saws use their real collision shape (triangle / circle) instead of the rough rectangle. |
| TRUE Hitboxes | Extends Accurate Hitboxes to every hazard object. Requires Accurate Hitboxes. |
| Noclip | Spikes cannot touch you and blocks are passable. |
| Force Ice | Every block becomes an ice block in platformer mode. |
| Force Platformer | Plays classic levels in platformer mode. |
| All Modes in Platformer | Plays platformer levels as Ship, Ball, UFO, Wave, Robot, Spider or Swing. Pick the mode in the mod settings. |
| Make Everything 3D | Applies radial blur and a bulge to every level, across shader layers B5 to Max. |

## Platform support

- Windows (Geometry Dash 2.2081)
- Android (Geometry Dash 2.2081)

Requires Geode 5.9.0 or newer.

## Eclipse integration

If [Eclipse](https://geode-sdk.org/mods/eclipse.eclipse-menu) is installed, the mod
automatically adds an `antika` tab with a toggle for every setting, kept in sync with
the Geode mod settings. Eclipse is an optional dependency — the mod works without it.

## Building

```sh
# requires GEODE_SDK to point at a Geode SDK checkout
geode build
```

## Credits

Built with the [Geode SDK](https://github.com/geode-sdk/geode).

The Negative Hitboxes technique (inverting object scale and disabling the level's
*Fix Negative Scale* option) was first popularised by
[Negative Hitboxes](https://geode-sdk.org/mods/demiomad.negative-hitboxes) by Demiomad.
This mod reimplements that technique as one toggle among several, and is declared
incompatible with it so the two cannot fight over object scale at the same time.
