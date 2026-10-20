# The game server and the lobby, as two images from one build:
#   docker build --target server -t ledgefall-server .
#   docker build --target lobby   -t ledgefall-lobby  .
#   docker build -t ledgefall .                           # both binaries: the deploy stack's image
# Both hold one binary on the same pinned Debian base, run as uid 65534, and need no
# capabilities and no volumes. The lobby is netplay-kit's (apps/npk_lobby.cpp), built from
# the commit CMakeLists.txt pins; the server is ledgefall's core behind netplay-kit's game
# server. CMake fetches both libraries (fighter-physics, netplay-kit) at their pinned commits.
#
# Why Debian and not scratch: a static glibc binary cannot resolve compose service names,
# and the server reaches the lobby by its service name.

FROM debian:trixie-slim@sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f AS build
RUN apt-get update \
    && apt-get install -y --no-install-recommends g++ cmake make git ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY include include
COPY src src
COPY server server
RUN cmake -S . -B /build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DLEDGEFALL_BUILD_SERVER=ON \
    && cmake --build /build --parallel --target ledgefall_server npk_lobby

FROM debian:trixie-slim@sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f AS lobby
COPY --from=build /build/_deps/netplay_kit-build/npk_lobby /npk-lobby
USER 65534:65534
EXPOSE 8080/tcp
ENTRYPOINT ["/npk-lobby"]

FROM debian:trixie-slim@sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f AS server
COPY --from=build /build/ledgefall_server /ledgefall-server
USER 65534:65534
EXPOSE 7777/udp
ENTRYPOINT ["/ledgefall-server"]

# The stack's image: both binaries (the compose file picks one per service with `entrypoint`), so the
# stack has one image and one pin. This is the last stage, so a plain `docker build .` makes it.
FROM debian:trixie-slim@sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f AS stack
COPY --from=build /build/_deps/netplay_kit-build/npk_lobby /npk-lobby
COPY --from=build /build/ledgefall_server /ledgefall-server
USER 65534:65534
EXPOSE 8080/tcp 7777/udp
