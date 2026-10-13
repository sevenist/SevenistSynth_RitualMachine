# Guides

Task-oriented guides for people working in this repo. The reference documents stay at the repo root:
[DEVELOPING.md](../../DEVELOPING.md) (the layout, the builds, where to change things), [ENGINE_DESIGN.md](../../ENGINE_DESIGN.md)
(the engine's decisions and measurements) and [CHANGELOG.md](../../CHANGELOG.md) (what changed for developers).

| Guide | For | Covers |
| --- | --- | --- |
| [DRAWING_KIT.md](DRAWING_KIT.md) | any app on the library | putting something on the screen with `framework/ui`: a frame from start to end, rectangles, text, fields, sprites, animations, popups |
| [RITUAL_MACHINE_UI.md](RITUAL_MACHINE_UI.md) | the Ritual Machine | how its screens are built: the main view's pages, the menu tabs as tables of elements, the Pads section, the browser, and where to make a change |
| [SERIAL_COMMANDS.md](SERIAL_COMMANDS.md) | the Ritual Machine on the first prototype | the dev commands over USB serial, what the board prints, `serial_test.py`, with examples |

Starting a new app: `apps/template/README.md`.
