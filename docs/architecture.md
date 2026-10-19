# Ledgefall: architecture (starting point)

How the three projects fit together. The project's agent keeps this in step
with the code; hard-to-reverse choices get a record in `docs/decisions/`.

```
            fighter-physics (C++ lib)          netplay-kit (C++ lib + services)
            deterministic parallel engine      transport, rollback session,
            movement coefficients, platforms   game server, lobby, home page
                     \                                /
                      \                              /
                 ledgefall core (C++ lib, this repo: core/)
                 game rules on top of the engine: characters, moves,
                 placeables, hazards, stages, stocks, bots; implements
                 netplay-kit's Simulation interface
                     /                               \
   macOS app (this repo: app/)                 game server (this repo: server/)
   SwiftUI shell + Metal renderer,             netplay-kit's game server +
   GameController (Xbox) + keyboard,           ledgefall core, Docker image,
   C++ core through Swift C++ interop,         runner lane on the PC
   netplay-kit client session
```

## Layout of this repository

| Path | What | Builds on |
| --- | --- | --- |
| `core/` (now `include/`, `src/`) | the game's simulation and rules (C++20) | Linux and macOS |
| `server/` | the game server executable and its `Dockerfile` | Linux (Docker) and macOS (natively, for development) |
| `app/` | the macOS app: Swift package or Xcode project, Metal shaders, input module | macOS only (GitHub's macos runners, the owner's Mac) |
| `deploy/stack/` | the runner-lane stack for the PC, once the server exists | |

The dependencies are fighter-physics and netplay-kit through CMake
`FetchContent` at pinned commits. The pin is bumped deliberately in a PR,
never by following a branch.

## The core (milestone 1, done)

`include/ledgefall/`, `src/`; tests in `tests/`. It has the shape of
netplay-kit's `Simulation` interface (`step(frame, inputs)`, `snapshot`,
`restore`, `hash`, input encode and decode), so the adapter in milestone 3 is
thin; netplay-kit's interface is not settled yet, and the adapter is where any
difference will be absorbed.

- `Setup` (2 to 4 players, 1 to 5 stocks, 3 by default) becomes `Rules`: the
  fighter-physics `Config` (a stage of three tiers of crumbling one-way tiles,
  a movement table per seat) and the spawn points. It never changes in a match.
- `State` is plain data: the engine's `fp::World` (entities indexed by seat),
  each player's stocks and respawn timer, and the result. `step(rules, state,
  inputs)` is a pure function of the state and one `Input` per seat (a move
  axis -100..100 and jump and down buttons, two bytes on the wire).
- An eliminated player loses a stock and returns 1.5 s later at the spawn point
  farthest from the others still in play. The last player with stocks wins. If
  the last players go out on the same tick, lost-footing breaks the tie by
  credited eliminations, fewer self-eliminations, platforms broken, then seat
  order; only the last step, the lowest seat, exists until placeables do.
- `hash`, `snapshot` and `restore` cover exactly the `State`; `restore`
  validates its bytes and leaves the state untouched on failure.
- Crumbling is lost-footing's stress model (fighter-physics `PlatformStress`,
  numbers in `src/stage.cpp`), and every match ends: overtime from 1:30 (each
  30 s a broken tile takes 50% longer to return), collapse from 3:30 (nothing
  broken returns; a random tile, from the match's seeded generator in the
  state, is doomed every 0.5 s).
- The kit (milestone 5, in progress): seven `Character`s with a movement table
  each (data, `src/characters.cpp`) and three moves on the Place, Trick and
  Break buttons; a move casts on the tick its button goes down if its cooldown
  (lost-footing's) is spent. Moves put things into the engine (`src/moves.cpp`):
  walls and springs are engine placeables, with a side table in the state for
  owner, uses left and the tile they stand on; a spring first waits out its
  arming time as a pending placement. Credit is `blame` (who last did something
  indirect to a player, within 5 s), stats, and lost-footing's tie-break for
  the placings (`rank`). Vertical contact (`src/contact.cpp`): a fast landing
  on a head is a stomp (bounce, slow, tile stress), a slow one rides.
  Stages are data (`src/stage.cpp`): lost-footing's layouts (0.32 units a pixel
  across, 0.26 up so one jump still reaches the next tier): Practice (a test
  floor), Crumble Yard, The Quarry. A sturdy slab is a platform on the stress
  model with no rates; its hit points are in the state, `damage_tile` breaks it
  at 0 (the engine sends it Gone and brings it back whole after 8 s), and the
  collapse wears slabs down. Done: Mason's Wall and Anvil, all of Fuse's kit (bumper, charge, saw), all of Pip's kit (Spring with its air launch, Gust, Drill), all of Glim's kit.
  Lobs and acid puddles are `Object`s in the state (`src/objects.cpp`); glue and
  oil become the engine's sticky and slick patches; tiles remember who last
  stressed them (`tile_blame`), so a tile that drops someone credits its maker.
  Everything else in
  `docs/lost-footing-brief.md`'s table follows one move at a time. Lost-footing
  pixels become units at about 0.32.
- Engine asks for the rest of the kit: one-way placeables (ledge, vine leaves,
  decoys; fighter-physics#21) and per-entity movement modifiers (glue, thorns;
  #22, worked around by scaling a copy of the config).
- Engine bug found: fighter-physics#23 (`restore` rejects a world with a doomed
  tile); rollback cannot ship until it is fixed and the pin bumped.
- Known gaps against lost-footing: the respawn halo and post-respawn immunity
  wait for hazards; the engine splits a straddler's load equally between the
  tiles (lost-footing: 75% each).

How determinism is kept honest (all in `./scripts/verify`, so CI runs it on
Linux and macOS): the golden hash trace of a scripted 4-player match
(`tests/golden/determinism_hashes.txt`), replay and repeated-rollback tests, and
`scripts/check-determinism-guards`, which fails on floats, the math library,
clocks, random sources, unordered containers or threads in the core. The engine
pin is bumped in a PR of its own (`docs/decisions/0001-engine-pin.md`).

## The app (milestone 2)

`app/` is a Swift package over the core's C face (`include/ledgefall/capi.h`,
raw Q16.16 integers, no floats), built by `scripts/build-app`, which
`./scripts/verify` runs on macOS after the core. CI's macos job builds it and
runs the XCTests; nothing in CI opens the window.

- `CLedgefall`: the C header as a module; the libraries come from CMake.
- `LedgefallKit`: everything testable without a window: the `Match` wrapper,
  the scene as a list of quads, the camera, key clusters (WASD, arrows, IJKL),
  gamepad mapping, `Seating` (drop-in: every seat starts as a bot, a device
  takes the lowest free seat by being used, an unplugged controller frees
  it), `Flow` (setup with players 2 to 4 and stocks 1 to 5, match, pause,
  results) and the fixed-tick `GameLoop`.
- `Ledgefall`: AppKit window, an `MTKView` drawing flat quads with a shader
  compiled from source at start-up, SwiftUI screens over it, and
  GameController.framework for Xbox and other extended gamepads.

Not built yet: rebinding, dead-zone settings, per-seat device menus, sound,
character art, interpolation between ticks. Bots are the footing bot only (see
`include/ledgefall/bot.hpp`).

## Input

The app's input module reads GameController.framework (Xbox and other MFi or
extended gamepads) and the keyboard. It maps them to ledgefall's actions
(move axis, jump, the placement and break moves, shield-like actions, menu)
and turns those into the engine's per-frame input bitfield.

- **Owned by the app:** dead zones, rebinding and per-player device
  assignment for local multiplayer.
- **Owned by the core:** input buffering windows, defined in frames, so
  client and server agree.
- **Later:** if a future game needs the same input layer, extract it into a
  library then, not before.

## Deterministic simulation

The core runs on fighter-physics' fixed timestep. A frame's state is a pure
function of the previous state and the frame's inputs, so netplay-kit can
roll it back. Client (arm64) and server (x86_64) must agree bit for bit:
fighter-physics owns the number representation, and a cross-platform hash
test runs in CI.

## Hosting

The server and netplay-kit's lobby deploy as containers on the Arch PC
through the scshafe runner lane (`host = "pc"`). The lobby is HTTP, so the
usual sidecar serves it. Game traffic is UDP: netplay-kit's first network
milestone proves the path through the tailnet node, and its design lists the
fallbacks. Players outside the tailnet get a Tailscale node share. A public
host is a later owner decision.

## First milestones

1. Done: core: characters moving on crumbling platforms with lost-footing's
   stocks and winner rules, headless, tested, on the fighter-physics movement
   model.
2. App (built; the window, shader and controller code are untested by anyone
   yet): a window that renders the core locally (Metal), with Xbox controller
   and keyboard input; local play against bots.
3. Server and online play: the core as netplay-kit's `Simulation`, a game
   server image, two Macs in one match over the tailnet.
4. The lobby and home page (netplay-kit), deployed on the PC.
5. Lost-footing's full kit: characters, placeables, hazards and stages.
