# 0002: netplay-kit is pinned by commit, and only the server uses it

Status: accepted, 2026-10-19 (the owner session asked for milestone 3 to start
once netplay-kit's `Simulation` interface landed).

## Context

netplay-kit supplies the transport, the rollback session, the game server and
the lobby. Its `Simulation` interface (`npk/session/simulation.hpp`) is what a
game implements. Ledgefall's core already has that shape: a fixed-tick `step`
over one two-byte input per seat, `snapshot`, `restore` and `hash`.

## Decision

- netplay-kit comes in with CMake `FetchContent` at a full commit SHA, pinned
  and bumped the way fighter-physics is (`0001-engine-pin.md`): in a PR of its
  own that changes only the SHA, never by following a branch. Its tests,
  sanitizers and default targets are off in our build (`EXCLUDE_FROM_ALL`); we
  build the library, and the server image builds its lobby.
- The dependency is a separate target, `ledgefall_netplay`, so the core and the
  macOS app do not link it. The core stays free of networking; the adapter
  (`include/ledgefall/netplay.hpp`) is the only place the two meet.
- The adapter owns the `Rules` and `State` of one match. It does not change what
  the core is: `step`, `snapshot`, `restore` and `hash` are the core's.
- A need netplay-kit lacks is raised with the owner session, not patched here.

Current pin: `75440f27a86081fde5c800aecd3b0597816c390f` (netplay-kit main after
its milestone 3: the transport, sessions, game server, lobby, executables).
