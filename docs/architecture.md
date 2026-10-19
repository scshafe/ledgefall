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

1. Core: one character moving on crumbling platforms with lost-footing's
   rules, headless, tested, on the fighter-physics movement model.
2. App: a window that renders the core locally (Metal), with Xbox controller
   and keyboard input; local play against bots.
3. Server and online play: the core as netplay-kit's `Simulation`, a game
   server image, two Macs in one match over the tailnet.
4. The lobby and home page (netplay-kit), deployed on the PC.
5. Lost-footing's full kit: characters, placeables, hazards and stages.
