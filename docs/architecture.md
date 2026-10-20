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
  floor), Crumble Yard, The Quarry, Tide Pools, Gale Bridge, Conveyor Sky (rows of tiles that carry whoever stands on them, using the engine's conveyor
  platforms, #24; a hazard stops them all for a warning and runs them the other
  way), Sinking Ship (decks heave on the engine's there-and-back
  platform motion in place of lost-footing's sine, halves out of step).
  Stage hazards (`src/hazards.cpp`) run on their rhythm from the match's random
  stream, each announced before it acts: comets skim lanes (a wall or bumper
  reflects one and its owner is credited with what it hits next; a gust can
  turn it), rocks fall (aimed at slabs, players or anywhere), the tide rises
  and drowns, a gale blows. Overtime shortens their gaps by 15% a step (to
  40%). A respawned player is untouchable by hazards and contact for 2 s.
  All of lost-footing's stages are here. A sturdy slab is a platform on the stress
  model with no rates; its hit points are in the state, `damage_tile` breaks it
  at 0 (the engine sends it Gone and brings it back whole after 8 s), and the
  collapse wears slabs down. Done: all of Fern's kit (the Vine: a seed that needs ground grows four one-way
  leaves in a zigzag, one every 0.2 s, each lasting about 7 s). The one-way placeables (fighter-physics#21; `src/oneway.cpp`): Mason's
  Ledge (hit points, one at a time), Dab's Decoy (solid for her alone; whoever
  falls through is credited to her) and Hollow (tiles shimmer, then go, and a floor
  only she holds replaces them for 2.5 s), and the respawn halo (a personal
  platform for 3 s that goes when its owner steps off). Done: all of Ferro's kit (lodestone, polarity, ingot: loose metal a lodestone pulls changes owner, and so does its credit), Mason's Wall and Anvil, Fern's Thorns and Roots, Dab's Rub out, all of Fuse's kit (bumper, charge, saw), all of Pip's kit (Spring with its air launch, Gust, Drill), all of Glim's kit.
  Lobs and acid puddles are `Object`s in the state (`src/objects.cpp`); glue and
  oil become the engine's sticky and slick patches; tiles remember who last
  stressed them (`tile_blame`), so a tile that drops someone credits its maker.
  Everything else in
  `docs/lost-footing-brief.md`'s table follows one move at a time. Lost-footing
  pixels become units at about 0.32.
- Movement modifiers (the engine's per-entity `Modifier`, set each tick in
  `apply_modifiers`): a stomp slows ground speed to 0.55, glue cuts the jump to
  0.55, brambles halve acceleration and cut the jump to 0.8 (their speed is the
  patch's).
- Engine bug found: fighter-physics#23 (`restore` rejects a world with a doomed
  tile); rollback cannot ship until it is fixed and the pin bumped.
- Bots use their kit (`src/bot.cpp`): they plan a move that suits the situation
  (walls and anvils for Mason, lobs that land near someone's feet for Glim
  and Fuse's charge, gust, spring and drill for Pip, saw and bumper for Fuse,
  thorns and roots for Fern, rub out for Dab, lodestone and polarity for
  Ferro), turn to face the target, and cast with the level's gap and chance.
  Their choices come from their own stream, not the match's. They also survive: a comet
  in their row is jumped, a rock's column left while it is announced, surfaces under
  the tide avoided, and a bot falling out of the stage flies back towards its middle
  and uses its character's recovery (Pip's air spring, Fuse's bumper underfoot,
  Mason's ledge, Dab's decoy; Glim has her second air jump). In a gale a bot in the
  air or on the downwind half of the stage leans into the wind, and prefers tiles
  upwind. On Conveyor Sky it counts a belt that sweeps the end of its row as a
  cost when it picks a tile (reading the direction the belts will run next during
  their stop), and at such an end it walks against the belt unless it means to drop.
  Easy bots miss hazards 45% of the time, normal 15%, hard never (lost-footing's rates).
- Teams and handicaps (`Setup::teams`, `seat_stocks`): the match ends when one
  side has anyone left and the whole side wins and places first; eliminations a
  teammate caused earn no credit; bots never target a teammate. Two sides are
  needed (A and B, or a team and a solo player), else everyone plays alone.
- Replays (`include/ledgefall/replay.hpp`): a match is its setup and every
  seat's input every tick (bots' too), written down by a `Recorder`, stored
  compactly (a tick stores only the seats whose input changed) and ending with
  the state's hash, which `verify` checks by running it again. `decode_replay`
  refuses anything that is not a well-formed replay of a valid setup. The C face
  records every match and plays one back (`lf_replay_*`).
- Known gaps against lost-footing: the engine splits a straddler's load equally
  between the tiles (lost-footing: 75% each).

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

The setup screen picks players, stocks, the stage, teams (off, 1+3 vs 2+4,
1+2 vs 3+4) and a character for each seat; the HUD shows each seat's character,
team and move cooldowns; a finished match's replay is saved to
`~/Library/Application Support/Ledgefall/Replays/` .

Online play (milestone 3, app side): the setup screen's Mode row chooses local, host or
join. Hosting makes a room through the lobby (`LobbyClient`, URLSession) with the setup in the
room's name (`OnlineSetup`: until the lobby shows a room's options to its players, the name is
how a joiner learns the stage and characters) and as the options the server reads, takes a seat,
readies, and waits; joining lists the open ledgefall rooms. When the lobby hands the room to a game
server the app opens an `OnlineSession` (`lf_net_*`: netplay-kit's client session running the core)
and draws its read-only match view with the same renderer. Several people can play online from one Mac (the
setup's *On this Mac* row): each takes a seat of her own and has a session of her own with the game
server, and the keyboard clusters and controllers are given out in the order they are first used, as in
a local match. The pause key leaves. At the results, Return plays again: whoever hosted opens a new room with the
same setup (a new seed) and the others wait for it to appear in the list, then sit down. The lobby's address is `LEDGEFALL_LOBBY` or `defaults write ledgefall lobby <url>`.

Replays: every finished match is saved (`ReplayLibrary`, `~/Library/Application Support/Ledgefall/Replays/`);
the setup screen's fourth Mode lists them (newest first, with stage, stocks, characters and length) and
`ReplayWatch` plays one back through the same renderer, stepping the recorded inputs; Return pauses, Esc
leaves.

Online matches are kept too. A client's simulation writes down the inputs of every frame it steps (a rollback
steps a frame again, and the latest stepping stands), and `OnlineMatch::replay()` hands them over once the match
is over *and* the session has confirmed every frame of it, since until then some inputs are guesses. The replay
is checked by playing it again (a resync leaves frames this client never stepped, and such a match is not
kept). The app asks at the results screen, twice a second, through the same `lf_match_replay_*` calls the
local replay uses; the file is the server's canonical input, the same on every client.

Not built yet: rebinding, dead-zone settings, per-seat device menus, sound,
character art, interpolation between ticks. Bots are the footing bot only (see
`include/ledgefall/bot.hpp`).

## Online play (milestone 3)

`include/ledgefall/netplay.hpp`, `src/netplay.cpp`, `server/main.cpp`. netplay-kit
is pinned by commit like the engine (`docs/decisions/0002-netplay-kit-pin.md`)
and only the `ledgefall_netplay` target and the server link it; the core and
the app do not.

- `NetplaySimulation` is the core as netplay-kit's `Simulation`: it owns one
  match's `Rules` and `State`; `step`, `snapshot`, `restore` and `hash` are the
  core's; the input is the core's two bytes (`valid_input` is `decode`); the
  match is `finished` when the core says it is over, and `result_json` reports
  winner, winning team, ticks, places, eliminations, falls and stocks. A late
  input is predicted as the same movement without a press (a move button is an
  event, not a hold).
- `make_netplay_simulation(players, options)` is the server's factory. The
  lobby passes a room's options on: `stocks`, `seed`, `stage`, `characters`,
  `teams`, `seat_stocks`, all optional and all checked; anything wrong makes the
  room refuse.
- `ledgefall_server` is `npk::gameserver::run` with that factory.
- `ledgefall_bot` plays through the real lobby and server (find or create a
  room, take a seat, ready, join over UDP, play with the core's bot AI until the
  match ends); `scripts/e2e` runs the real executables end to end.
- The deployable stack is under `deploy/stack/` (README: what the host has),
  deployed to the PC by the runner lane (`dev.toml [deploy]`). One image, built by the root `Dockerfile`, holds the lobby and the
  server; both run in the Tailscale sidecar's network namespace.
- The client end (`include/ledgefall/online.hpp`, `src/online.cpp`, `src/capi_net.cpp`):
  `OnlineMatch` is netplay-kit's `ClientSession` running the core, and the C face
  (`lf_net_*`) wraps it over a UDP socket for the app, with a read-only `lf_match`
  view of the match as the client has it. The app links `ledgefall_netclient`: only
  netplay-kit's transport and session sources, compiled at the app's macOS floor
  (the rest of netplay-kit needs macOS 26 for `std::from_chars`), and the app
  talks to the lobby with `URLSession`.
- A snapshot is under 16 KiB; netplay-kit's reliable message holds 64 KiB.
- The simulated-network test (a server and two or four clients on a lossy
  link) converges to the state a straight replay of the canonical inputs gives.
  It needs an engine that can restore a world with a doomed tile
  (fighter-physics#23); until the pin has the fix the test says it is skipped.

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
