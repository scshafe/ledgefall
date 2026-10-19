import XCTest
@testable import LedgefallKit

final class MatchTests: XCTestCase {
    func testSetupLimits() {
        XCTAssertNil(Match(players: 1))
        XCTAssertNil(Match(players: 5))
        XCTAssertNil(Match(players: 2, stocks: 0))
        XCTAssertNil(Match(players: 2, stocks: 6))
        XCTAssertNotNil(Match(players: 4, stocks: 5))
    }

    func testStartingState() throws {
        let match = try XCTUnwrap(Match(players: 3))
        XCTAssertEqual(match.players, 3)
        XCTAssertEqual(match.player(0).stocks, 3)
        XCTAssertEqual(match.tick, 0)
        XCTAssertFalse(match.isOver)
        XCTAssertGreaterThan(match.platformCount, 0)
        XCTAssertEqual(match.platform(0)?.phase, .solid)
        let blast = match.blast
        XCTAssertLessThan(blast.left, blast.right)
        XCTAssertLessThan(blast.bottom, blast.top)
    }

    func testBotMatchEndsWithAWinner() throws {
        let match = try XCTUnwrap(Match(players: 2, stocks: 1))
        for seat in 0..<2 { XCTAssertTrue(match.setBot(seat: seat, level: .normal)) }
        var ticks = 0
        while !match.isOver && ticks < 6 * 60 * 60 {
            match.step([])
            ticks += 1
        }
        XCTAssertTrue(match.isOver)
        XCTAssertEqual(match.tick, ticks)
        XCTAssertNotNil(match.winner)
    }

    func testSameInputsSameHash() throws {
        func run() throws -> UInt64 {
            let match = try XCTUnwrap(Match(players: 2))
            for t in 0..<300 {
                match.step([PlayerInput(moveX: Int8(t % 3 - 1) * 100, jump: t % 20 == 0), PlayerInput()])
            }
            return match.hash
        }
        XCTAssertEqual(try run(), try run())
    }
}

final class SceneTests: XCTestCase {
    func testQuadsCoverPlatformsBodiesAndStocks() throws {
        let match = try XCTUnwrap(Match(players: 2, stocks: 3))
        let quads = Scene.quads(for: match)
        // Every platform is solid at the start, plus 2 bodies and 2 x 3 stock pips.
        XCTAssertEqual(quads.count, match.platformCount + 2 + 6)
    }

    func testCameraFitsTheBlastZone() {
        let blast = (left: Float(-130), right: Float(130), bottom: Float(-60), top: Float(160))
        for (w, h) in [(Float(1280), Float(720)), (Float(600), Float(900)), (Float(800), Float(800))] {
            let cam = Camera(blast: blast, viewWidth: w, viewHeight: h)
            for (x, y) in [(blast.left, blast.bottom), (blast.right, blast.top)] {
                let cx = x * cam.scale.x + cam.offset.x
                let cy = y * cam.scale.y + cam.offset.y
                XCTAssertLessThanOrEqual(abs(cx), 1.0001)
                XCTAssertLessThanOrEqual(abs(cy), 1.0001)
            }
            // Equal world units are equal on screen on both axes.
            XCTAssertEqual(cam.scale.x * w, cam.scale.y * h, accuracy: 0.01)
        }
    }
}

final class KeyboardTests: XCTestCase {
    func testArrowsAndWasd() {
        XCTAssertEqual(KeyLayout.arrows.input(held: [124]), PlayerInput(moveX: 100))
        XCTAssertEqual(KeyLayout.arrows.input(held: [123, 126]), PlayerInput(moveX: -100, jump: true))
        XCTAssertEqual(KeyLayout.arrows.input(held: [123, 124]), PlayerInput())
        XCTAssertEqual(KeyLayout.wasd.input(held: [2, 1]), PlayerInput(moveX: 100, down: true))
        XCTAssertEqual(KeyLayout.wasd.input(held: [124]), PlayerInput())
    }
}

final class GameLoopTests: XCTestCase {
    func testFixedTicksFromAVariableClock() throws {
        let loop = GameLoop(match: try XCTUnwrap(Match(players: 2)))
        loop.advance(to: 10.0) { [] }                  // the first call only sets the clock
        XCTAssertEqual(loop.match.tick, 0)
        loop.advance(to: 10.0 + 1.0 / 60 * 3.5) { [] }  // 3 whole ticks
        XCTAssertEqual(loop.match.tick, 3)
        loop.advance(to: 10.0 + 1.0 / 60 * 4.5) { [] }  // the half tick carried over completes a fourth
        XCTAssertEqual(loop.match.tick, 4)
        loop.advance(to: 100.0) { [] }                  // a long stall runs a bounded number of ticks
        XCTAssertEqual(loop.match.tick, 4 + GameLoop.maxTicksPerFrame)
    }
}

final class GamepadTests: XCTestCase {
    func testDeadZoneAndScaling() {
        XCTAssertEqual(GamepadMapping.input(GamepadState(leftX: 0.19)).moveX, 0)
        XCTAssertEqual(GamepadMapping.input(GamepadState(leftX: -0.2)).moveX, 0)
        XCTAssertEqual(GamepadMapping.input(GamepadState(leftX: 1)).moveX, 100)
        XCTAssertEqual(GamepadMapping.input(GamepadState(leftX: -1)).moveX, -100)
        XCTAssertEqual(GamepadMapping.input(GamepadState(leftX: 0.6)).moveX, 50)
        XCTAssertEqual(GamepadMapping.input(GamepadState(leftX: 5)).moveX, 100)  // out of range is clamped
    }

    func testDpadBeatsStickAndCancels() {
        XCTAssertEqual(GamepadMapping.input(GamepadState(leftX: -1, dpadRight: true)).moveX, 100)
        XCTAssertEqual(GamepadMapping.input(GamepadState(dpadLeft: true, dpadRight: true)).moveX, 0)
    }

    func testButtons() {
        XCTAssertTrue(GamepadMapping.input(GamepadState(south: true)).jump)
        XCTAssertTrue(GamepadMapping.input(GamepadState(north: true)).jump)
        XCTAssertFalse(GamepadMapping.input(GamepadState(east: true, west: true)).jump)
        XCTAssertTrue(GamepadMapping.input(GamepadState(dpadDown: true)).down)
        XCTAssertTrue(GamepadMapping.input(GamepadState(leftY: -0.7)).down)
        XCTAssertFalse(GamepadMapping.input(GamepadState(leftY: -0.5)).down)
    }
}

final class SeatingTests: XCTestCase {
    func testDevicesJoinOnUseInOrder() {
        var seating = Seating()
        var update = seating.update(DeviceFrame())
        XCTAssertEqual(update.joined, [])
        XCTAssertEqual(update.inputs, Array(repeating: PlayerInput(), count: 4))

        update = seating.update(DeviceFrame(keys: [124]))  // an arrow key
        XCTAssertEqual(update.joined, [0])
        XCTAssertEqual(seating.seat(of: .keyboard(cluster: 1)), 0)
        XCTAssertEqual(update.inputs[0], PlayerInput(moveX: 100))

        update = seating.update(DeviceFrame(keys: [124], pads: [7: GamepadState(south: true)]))
        XCTAssertEqual(update.joined, [1])
        XCTAssertEqual(update.inputs[1], PlayerInput(jump: true))

        update = seating.update(DeviceFrame(keys: [124], pads: [7: GamepadState()]))  // seated: stays, even idle
        XCTAssertEqual(update.joined, [])
        XCTAssertEqual(seating.seat(of: .controller(id: 7)), 1)
    }

    func testControllerUnplugFreesTheSeat() {
        var seating = Seating()
        _ = seating.update(DeviceFrame(pads: [3: GamepadState(south: true)]))
        XCTAssertEqual(seating.seat(of: .controller(id: 3)), 0)
        let update = seating.update(DeviceFrame())
        XCTAssertEqual(update.left, [0])
        XCTAssertNil(seating.seat(of: .controller(id: 3)))
        // The freed seat is the next to be taken.
        XCTAssertEqual(seating.update(DeviceFrame(keys: [0])).joined, [0])
    }

    func testFullSeatsTurnDevicesAway() {
        var seating = Seating()
        let pads = Dictionary(uniqueKeysWithValues: (0..<5).map { ($0, GamepadState(south: true)) })
        let update = seating.update(DeviceFrame(pads: pads))
        XCTAssertEqual(update.joined, [0, 1, 2, 3])
        XCTAssertNil(seating.seat(of: .controller(id: 4)))
    }

    func testIdleKeysOutsideAClusterDoNotJoin() {
        var seating = Seating()
        XCTAssertEqual(seating.update(DeviceFrame(keys: [49, 36])).joined, [])  // space, return
    }
}
