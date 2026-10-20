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

<!-- scshafe-dev:begin landing -->
## Verify and landing

Managed by scshafe-dev: `dev adopt` and `dev update` refresh this section from `dev.toml`; change `dev.toml`, not these lines.

Before finishing, both of these must pass:

```sh
./scripts/verify
dev check .
```

How a change lands:

1. Work on a branch and open a PR.
2. Run the two commands above. If the repository is private, GitHub Actions does not run for it: verify locally and say in the PR what you ran. If it is public, wait for CI to be green.
3. Merge your own PR with a merge commit, one change at a time: `gh pr merge <N> --merge --subject "Merge #<N>: <title>"`. Never squash or rebase (both are off on the repository), and pass `--subject`: `gh pr merge` does not make the `Merge #N: <title>` subject by itself.

The project's agent may merge its own PR and push `main`; there is no approval gate.

Merging deploys to production ([deploy] lane `runner`: `.github/workflows/deploy.yml` verifies on a GitHub-hosted runner, deploys through the host entrypoint on the `ledgefall-prod` self-hosted runner, then checks health).
Watch the run yourself with `gh run list -w deploy`, `gh run watch <id>` and `gh run view <id> --log` (a public repository's deploy log is a summary only); say in your reply what the run did, naming the merge commit.
Roll back by merging a `git revert`, or by dispatching `deploy.yml` with `sha=<older commit on main>` and `allow_rollback=true` (`gh workflow run deploy.yml -f sha=<sha> -f allow_rollback=true`).
<!-- scshafe-dev:end landing -->
