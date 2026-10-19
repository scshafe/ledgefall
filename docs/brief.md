# Ledgefall: the owner's brief

The owner's request, 2026-10-18 (lightly edited for reading):

> I really like the lost-footing game overall. I'd like to create a new
> separate project for a game that runs natively on macOS, with a server that
> is written entirely in C++. [...] I'd like the overall game to be pretty
> similar in gameplay to lost-footing otherwise. Also, the game needs to be
> playable by multiple players over the internet connecting to the same
> server. Also, I'd like this game to be playable with an Xbox controller
> (connected to macOS). [...] The server should be able to run on this Arch
> machine.

The owner's choices on 2026-10-18:

- **Three projects:**
  - ledgefall: this repository, the macOS client and the game server;
  - [fighter-physics](https://github.com/scshafe/fighter-physics): the
    engine;
  - [netplay-kit](https://github.com/scshafe/netplay-kit): the networking
    framework, with the home page, lobby and game server design.
- **Client:** a Swift and Metal macOS app shell that calls the shared C++
  core.
- **Controller input:** a module in this game, not its own project. macOS
  supports Xbox controllers natively.
- **Hosting:** the server runs in Docker on the Arch PC through the runner
  lane. Players outside the tailnet get a Tailscale node share. Public
  hosting comes later.
- **Licence:** public, MIT.

## Fixed points

- **Lost-footing's gameplay.** Its brief (`docs/lost-footing-brief.md`, copied
  here) holds, with two changes:
  - controllers replace the shared keyboard;
  - players play online.

  The rules in short:
  - no direct hits;
  - crumbling platforms are the stage;
  - moves place mobility objects and break platforms;
  - sturdy platforms are never safe for long;
  - body contact is vertical only;
  - creativity is rewarded.
- **Native macOS.** The app is a real Mac app: Swift, Metal rendering and
  GameController.framework. It supports Xbox controllers, with the keyboard
  as a fallback, and several local players on one Mac can join an online
  match.
- **A C++ server.** The game server is C++ (netplay-kit's game server
  embedding the game's simulation). It runs in Docker on Linux, on the Arch
  PC first.
- **Online multiplayer.** Players on different networks join the same match.
  The netcode suits a fighter: rollback on a deterministic simulation, and
  the netplay-kit design records the model.
- **Smash-style feel.** Each character's movement is data, the full
  fighter-physics coefficient table: dash, run, air speeds, gravity, fall
  speeds and jumps.
- **Bots** fill empty seats, as in lost-footing.
