# 0001: The engine is pinned by commit

Status: accepted, 2026-10-19.

## Context

Client (arm64) and server (x86_64) must agree bit for bit
(`docs/architecture.md`, "Deterministic simulation"). The engine owns the
number representation and the movement model, so an engine change can change
every hash.

## Decision

- fighter-physics comes in with CMake `FetchContent` at a full commit SHA in
  the top `CMakeLists.txt`; never a branch or a tag that can move.
- The pin is bumped in a PR of its own that changes only the SHA (and what
  must follow from it, such as golden hashes), and says which engine changes
  it takes in. A bump is not mixed with a rules change.
- Our build turns the engine's tests and benchmark off; they run in the
  engine's CI. Our own determinism tests are the gate here.
- A need the engine lacks is an issue in `scshafe/fighter-physics`, not a
  patch here; a small seam in the core covers the gap until it lands.

## Pins so far

- `953496fb410f80933067669eff409b6ad7f5845e`: engine milestones 1 to 6 (fixed
  point, movement, platforms, contacts, jobs, hash and snapshot).
- `546316bc15c625da43131c918ecb0975ccf4f325`: adds crumbling by accumulated
  stress, walls, slick and sticky patches and springs (fighter-physics #16 to
  #20). The match played is unchanged (same winner, same tick count); every
  hash differs because the engine's snapshot now carries stress and the
  placeable slots, so `tests/golden/determinism_hashes.txt` was regenerated.
- `3c6942ce50c86380d84245003181379bd4f20f01`: fighter-physics#25, restore accepts a doomed tile (#23). Snapshot
  format and pinned hashes unchanged; nothing else in the engine changed.
