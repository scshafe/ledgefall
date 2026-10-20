# The ledgefall stack

netplay-kit's lobby and ledgefall's game server, behind one Tailscale node `ledgefall`, on the PC.
It reaches production through the runner lane (`dev.toml [deploy]`, `.github/workflows/deploy.yml`;
scshafe/infra `docs/platform/agent-deploy.md`): a merge to `main` is verified on a GitHub-hosted
runner, then deployed by this repository's self-hosted runner through the host's entrypoint. The
host's allowances are infra's `stacks/ledgefall/host.conf`.

- Lobby: `https://ledgefall.<tailnet>/` (tailnet only; never Funnel). Game server: UDP 7777 on the
  node.

```
 player ---HTTPS (tailscale serve)---> lobby 127.0.0.1:8080  <---register, results, assign--->  game server
   |                                      (one network namespace: the sidecar's)                 127.0.0.1:9000 control
   +------------ UDP 7777 on the node's tailnet address, Join(room, token), then the match ------> game server
```

- `ts-ledgefall`: the Tailscale sidecar (userspace mode), image pinned by digest (v1.102.5, the
  version netplay-kit measured with).
- `lobby` and `gameserver`: the same image (`../../Dockerfile`, last stage: both binaries, uid 65532,
  read-only, no capabilities), each started with its own `entrypoint`. Both run in the sidecar's network
  namespace, so they reach each other on `127.0.0.1` (a userspace sidecar only forwards inbound, so a
  second namespace would have no way to reach the lobby by name or address). Nothing is published on
  the host.
- `serve.json` proxies HTTPS on the node's name to the lobby (`127.0.0.1:8080`, as the validator
  requires).

## What the host has

1. **A stack and a node**: stack `ledgefall`, tailnet node `ledgefall` (the names in `compose.yaml`, change
   them together), `door = "none"` (the lobby has no sign-in yet; the lobby and the game are reachable by
   whoever is on the tailnet or has the node shared). The sidecar subnet is gone: the stack has one
   `egress` bridge for the sidecar only.
2. **`stacks/ledgefall/host.conf`** in infra: nothing beyond the sidecar defaults is asked for: no host
   paths, no loopback ports, no groups. State is `./state/tailscale` only; there is no app data to back up
   (rooms and matches live in memory; a restart loses the matches in progress, and the lobby marks them
   failed).
3. **UDP 7777 inbound** on the node's tailnet address, for the game server. With the tailnet policy on
   allow-all it needs nothing; when the policy is narrowed it needs a grant for the players, for example
   `{src: [<players>], dst: ["tag:ledgefall"], ip: ["udp:7777"]}` (netplay-kit's docs/design.md "Network
   path"). TCP 443 on the node is `tailscale serve`'s, as for any stack.
4. **Secrets and values by name** (`.env.example`, `stack.toml [env]`; the agent has none and puts none in
   a file; the owner places them in the host's `/srv/stacks/ledgefall/.env`):
   - `NPK_CONTROL_SECRET`: generated, 16+ random characters, shared by the lobby and the game server
     (compared in constant time, never logged, never in a URL or a command line);
   - `NPK_PUBLIC_UDP`: required, `ledgefall.<tailnet>.ts.net:7777`: what the lobby tells players to
     connect to for the match;
   - `TS_AUTHKEY`: the sidecar's one-time key, first start only (owner);
   - `LEDGEFALL_IMAGE`: the pin the deploy writes.
5. **The image build** (`docker build .` from the repository root) needs the runner to reach GitHub
   (CMake fetches fighter-physics and netplay-kit at their pinned commits) and Debian's package mirror.
   The base image is pinned by digest.
6. **Health**: `stack.toml` probes `/api/status` through the node's name (the lobby has no
   `/healthz`; it answers `{"rooms":..,"game_servers":..}` without a secret), and the lobby's compose
   healthcheck asks the same path inside the namespace.
7. **The runner lane**: `dev.toml [deploy]` and `.github/workflows/deploy.yml` (rendered by
   `dev adopt --add-missing --lane runner --stack ledgefall --host pc --layout app --node ledgefall
   --door none`), the self-hosted runner registered on the PC for this repository, and the tailnet
   tag `tag:ledgefall` (infra `tailnet/policy.hujson`) on the sidecar's auth key.

## Known limits

- The game server's control API listens in the sidecar's namespace, where under userspace Tailscale a
  tailnet peer can reach it as it can reach every listening port; the shared secret is what protects it.
- A room's options (stage, characters, seed...) are public to anyone who can list rooms: they are what
  lets every client build the same game as the server. Nothing secret goes in them.
- Plain HTTP inside the namespace; `tailscale serve` terminates TLS for players.

## Checking it

```sh
python3 /usr/local/lib/scshafe/dev/current/scshafe_dev/vendor/stack_validate.py --stack ledgefall \
    --node ledgefall --door none --compose deploy/stack/compose.yaml --serve deploy/stack/serve.json \
    --stack-toml deploy/stack/stack.toml --no-interpolate      # ok (3 services)
docker build .                                                 # the image
scripts/e2e build                                              # the real executables, no containers
```

Run on the PC as the agent (rootless Docker): the image builds; the lobby and the game server start in
one namespace, the server registers with the lobby, and two bots play a match through them to its end.
