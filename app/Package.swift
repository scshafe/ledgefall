// swift-tools-version:5.9
// The macOS app: a Swift package over the C++ core's C face (include/ledgefall/capi.h).
// Build it with ../scripts/build-app, which builds the core with CMake first and
// passes the library search paths; it builds only on macOS.
import PackageDescription

let package = Package(
    name: "Ledgefall",
    platforms: [.macOS(.v13)],
    products: [
        .executable(name: "Ledgefall", targets: ["Ledgefall"]),
    ],
    targets: [
        // The core's C header as a module; the libraries come from CMake (see module.modulemap).
        .systemLibrary(name: "CLedgefall", path: "Sources/CLedgefall"),
        // Everything that is not AppKit or Metal: the match wrapper, input mapping, the scene, the camera.
        .target(name: "LedgefallKit", dependencies: ["CLedgefall"]),
        // The window, the Metal renderer and the game loop.
        .executableTarget(name: "Ledgefall", dependencies: ["LedgefallKit"]),
        .testTarget(name: "LedgefallKitTests", dependencies: ["LedgefallKit"]),
    ]
)
