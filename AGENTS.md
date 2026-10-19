# ledgefall agent notes

- An online survival platform fighter for macOS. The repository holds the
  shared C++ core (the rules), the C++ game server (Docker, on the PC) and
  the Swift + Metal macOS app (`docs/brief.md` is fixed;
  `docs/architecture.md` is the designer's, kept in step with the code).
- Lost-footing's rules are the game's rules (`docs/lost-footing-brief.md`).
  Ledgefall changes only the input (controllers) and the place of play
  (online).
- The engine is fighter-physics and the networking is netplay-kit, both
  pulled in with CMake `FetchContent` at pinned commits. A change that
  belongs in one of them is a PR there, not a patch here.
- The simulation must stay deterministic: no floats in game state, no
  unordered iteration that leaks into state, no wall-clock reads. The
  cross-platform hash test is the gate.
- The macOS app builds only on macOS: CI's macos job and the owner's Mac.
  This PC has no Swift. Say in the PR what you could not build.
- Deploying, tailnet nodes, node shares and runner registration are the
  owner session's work: put what you need in the PR and ask.
- Public, MIT. CI runs on GitHub: wait for it before merging.
