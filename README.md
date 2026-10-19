# Ledgefall

An online survival platform fighter for macOS, the successor to
[lost-footing](https://github.com/scshafe/lost-footing). Nobody can hit
anybody: you win by making the ground go away. It has Smash-style movement,
Xbox controller support, and online matches on a C++ server.

- Brief: [docs/brief.md](docs/brief.md). Architecture: [docs/architecture.md](docs/architecture.md).
- Built on [fighter-physics](https://github.com/scshafe/fighter-physics) (the
  engine) and [netplay-kit](https://github.com/scshafe/netplay-kit) (the
  networking framework).

## Build and test

```sh
./scripts/verify      # the C++ core and server: configure, build, test (CMake + CTest)
```

The macOS app (`app/`, once it exists) builds on macOS only.

## License

MIT (`LICENSE`).
