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
        XCTAssertEqual(match.player(0).character, .mason)
        XCTAssertEqual(match.player(0).cooldown, [0, 0, 0])
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

    func testChoosingCharacters() throws {
        let match = try XCTUnwrap(Match(players: 2, characters: [.ferro, .fern]))
        XCTAssertEqual(match.player(0).character, .ferro)
        XCTAssertEqual(match.player(1).character, .fern)
        XCTAssertNotNil(Match(players: 3, characters: [.mason]))  // a short list falls back to the defaults
    }

    func testThingsStartEmptyAndSlabsAreSturdy() throws {
        let match = try XCTUnwrap(Match(players: 2, arena: .yard))
        XCTAssertEqual(match.thingCount, 2)  // a halo under each player
        XCTAssertEqual(match.thing(0)?.kind, .halo)
        let sturdy = (0..<match.platformCount).compactMap { match.platform($0) }.filter { $0.sturdy }
        XCTAssertEqual(sturdy.count, 3)
        XCTAssertEqual(sturdy.first?.hp, 4)
    }

    func testStages() throws {
        for arena in Arena.allCases {
            let match = try XCTUnwrap(Match(players: 4, arena: arena))
            XCTAssertGreaterThan(match.platformCount, 10)
        }
        let yard = try XCTUnwrap(Match(players: 2, arena: .yard))
        let practice = try XCTUnwrap(Match(players: 2, arena: .practice))
        XCTAssertNotEqual(yard.platformCount, practice.platformCount)
        XCTAssertEqual(yard.weather.tidePhase, 0)
        XCTAssertEqual(yard.weather.galePhase, 0)
    }

    func testTeamsAndHandicaps() throws {
        let match = try XCTUnwrap(Match(players: 3, stocks: 3, teams: [0, 0, 1], seatStocks: [0, 0, 5]))
        XCTAssertEqual(match.player(0).team, 0)
        XCTAssertEqual(match.player(2).team, 1)
        XCTAssertEqual(match.player(2).stocks, 5)
        XCTAssertEqual(match.player(0).stocks, 3)
        XCTAssertNil(match.winningTeam)
        // One side only: everyone plays alone.
        let alone = try XCTUnwrap(Match(players: 3, teams: [0, 0, 0]))
        XCTAssertEqual(alone.player(0).team, -1)
        XCTAssertNil(Match(players: 3, seatStocks: [10, 0, 0]))
    }

    func testReplayReproducesTheMatch() throws {
        let match = try XCTUnwrap(Match(players: 2, stocks: 1, seed: 3))
        for seat in 0..<2 { match.setBot(seat: seat, level: .hard) }
        var ticks = 0
        while !match.isOver && ticks < 6 * 60 * 60 {
            match.step([])
            ticks += 1
        }
        XCTAssertTrue(match.isOver)
        let data = try XCTUnwrap(match.replayData())
        let replay = try XCTUnwrap(Replay(data: data))
        XCTAssertTrue(replay.isGenuine)
        XCTAssertEqual(replay.ticks, match.tick)
        let watch = try XCTUnwrap(replay.makeMatch())
        for t in 0..<replay.ticks { watch.step(replay.inputs(at: t)) }
        XCTAssertEqual(watch.hash, match.hash)
        XCTAssertEqual(replay.inputs(at: replay.ticks), [])
        XCTAssertNil(Replay(data: Data([1, 2, 3])))
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
        XCTAssertEqual(KeyLayout.wasd.input(held: [12, 14, 15]), PlayerInput(place: true, trick: true, brk: true))
        XCTAssertEqual(KeyLayout.arrows.input(held: [47]), PlayerInput(place: true))
        XCTAssertEqual(KeyLayout.ijkl.input(held: [35]), PlayerInput(brk: true))
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
        XCTAssertFalse(GamepadMapping.input(GamepadState(north: true)).jump)
        XCTAssertEqual(GamepadMapping.input(GamepadState(west: true)), PlayerInput(place: true))  // X
        XCTAssertEqual(GamepadMapping.input(GamepadState(east: true)), PlayerInput(trick: true))  // B
        XCTAssertEqual(GamepadMapping.input(GamepadState(north: true)), PlayerInput(brk: true))   // Y
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

final class FlowTests: XCTestCase {
    /// Move the cursor to a setup row.
    private func go(_ flow: inout Flow, to row: Int) {
        while flow.selectedRow > row { flow.handle(.up) }
        while flow.selectedRow < row { flow.handle(.down) }
    }

    func testSetupDefaultsAndLimits() {
        var flow = Flow()
        XCTAssertEqual(flow.phase, .setup)
        XCTAssertEqual(flow.stocks, 3)
        XCTAssertEqual(flow.players, 2)
        XCTAssertEqual(flow.arena, .yard)
        XCTAssertEqual(flow.teamMode, .off)
        XCTAssertEqual(flow.selectedRow, flow.startRow)
        go(&flow, to: Flow.playersRow)
        flow.handle(.up)  // clamped
        for _ in 0..<5 { flow.handle(.right) }
        XCTAssertEqual(flow.players, 4)
        XCTAssertEqual(flow.rows, Flow.firstSeatRow + 4 + 1)
        for _ in 0..<5 { flow.handle(.left) }
        XCTAssertEqual(flow.players, 2)
        go(&flow, to: Flow.stocksRow)
        for _ in 0..<9 { flow.handle(.right) }
        XCTAssertEqual(flow.stocks, 5)
        for _ in 0..<9 { flow.handle(.left) }
        XCTAssertEqual(flow.stocks, 1)
    }

    func testStageTeamsAndFightersCycle() {
        var flow = Flow()
        go(&flow, to: Flow.stageRow)
        flow.handle(.right)
        XCTAssertEqual(flow.arena, .quarry)
        for _ in 0..<Arena.allCases.count { flow.handle(.left) }  // all the way round
        XCTAssertEqual(flow.arena, .quarry)
        go(&flow, to: Flow.teamsRow)
        flow.handle(.right)
        XCTAssertEqual(flow.teamMode, .alternating)
        flow.handle(.left)
        flow.handle(.left)
        XCTAssertEqual(flow.teamMode, .halves)  // wraps
        go(&flow, to: Flow.firstSeatRow + 1)
        flow.handle(.right)
        XCTAssertEqual(flow.fighters[1], .pip)  // after Glim
        flow.handle(.left)
        flow.handle(.left)
        XCTAssertEqual(flow.fighters[1], .mason)
        flow.handle(.left)
        XCTAssertEqual(flow.fighters[1], .ferro)  // wraps
        go(&flow, to: Flow.firstSeatRow + 1)
        XCTAssertEqual(flow.fighters[0], .mason)  // the other seats are untouched
    }

    func testSeatRowsOnlyForTheSeatsInTheMatch() {
        var flow = Flow()
        XCTAssertEqual(flow.rows, Flow.firstSeatRow + 2 + 1)
        // With two players the row after the second seat is Start, not a third seat.
        go(&flow, to: Flow.firstSeatRow + 1)
        flow.handle(.down)
        XCTAssertEqual(flow.selectedRow, flow.startRow)
    }

    func testTeamModes() {
        XCTAssertEqual(TeamMode.off.teams(players: 3), [nil, nil, nil])
        XCTAssertEqual(TeamMode.alternating.teams(players: 4), [0, 1, 0, 1])
        XCTAssertEqual(TeamMode.halves.teams(players: 4), [0, 0, 1, 1])
        XCTAssertEqual(TeamMode.halves.teams(players: 3), [0, 0, 1])
        XCTAssertEqual(TeamMode.halves.teams(players: 2), [0, 1])
    }

    func testConfirmWalksDownToStart() {
        var flow = Flow()
        go(&flow, to: Flow.firstSeatRow + 1)
        XCTAssertNil(flow.handle(.confirm))  // the last seat -> start
        XCTAssertEqual(flow.selectedRow, flow.startRow)
        XCTAssertEqual(flow.handle(.confirm), .startMatch)
        XCTAssertEqual(flow.phase, .playing)
    }

    func testPauseResumeAndQuit() {
        var flow = Flow()
        XCTAssertEqual(flow.handle(.confirm), .startMatch)  // starts on the Start row
        XCTAssertNil(flow.handle(.left))                    // only back matters while playing
        XCTAssertEqual(flow.phase, .playing)
        flow.handle(.back)
        XCTAssertEqual(flow.phase, .paused)
        flow.handle(.confirm)
        XCTAssertEqual(flow.phase, .playing)
        flow.handle(.back)
        XCTAssertEqual(flow.handle(.back), .showDemo)
        XCTAssertEqual(flow.phase, .setup)
    }

    func testPauseButtonPausesAndResumes() {
        var flow = Flow()
        flow.handle(.confirm)
        flow.handle(.pause)
        XCTAssertEqual(flow.phase, .paused)
        flow.handle(.pause)
        XCTAssertEqual(flow.phase, .playing)
    }

    func testResultsRematchOrSetup() {
        var flow = Flow()
        flow.matchOver(winner: 1)  // not playing: ignored
        XCTAssertEqual(flow.phase, .setup)
        flow.handle(.confirm)
        flow.matchOver(winner: 1)
        XCTAssertEqual(flow.phase, .results(winner: 1))
        XCTAssertEqual(flow.handle(.confirm), .startMatch)
        flow.matchOver(winner: nil)
        XCTAssertEqual(flow.phase, .results(winner: nil))
        XCTAssertEqual(flow.handle(.back), .showDemo)
        XCTAssertEqual(flow.phase, .setup)
    }
}

final class MenuInputTests: XCTestCase {
    func testKeys() {
        XCTAssertEqual(MenuInput.action(forKey: 36), .confirm)
        XCTAssertEqual(MenuInput.action(forKey: 49), .confirm)
        XCTAssertEqual(MenuInput.action(forKey: 53), .back)
        XCTAssertEqual(MenuInput.action(forKey: 126), .up)
        XCTAssertNil(MenuInput.action(forKey: 0))
    }

    func testControllerEdgesOnly() {
        let idle = GamepadState()
        XCTAssertEqual(MenuInput.actions(from: idle, to: GamepadState(south: true)), [.confirm])
        XCTAssertEqual(MenuInput.actions(from: GamepadState(south: true), to: GamepadState(south: true)), [])
        XCTAssertEqual(MenuInput.actions(from: idle, to: GamepadState(east: true)), [.back])
        XCTAssertEqual(MenuInput.actions(from: idle, to: GamepadState(menu: true)), [.pause])
        XCTAssertEqual(MenuInput.actions(from: idle, to: GamepadState(dpadRight: true)), [.right])
        XCTAssertEqual(MenuInput.actions(from: idle, to: GamepadState(leftY: -0.9)), [.down])
        XCTAssertEqual(MenuInput.actions(from: GamepadState(leftY: -0.9), to: GamepadState(leftY: -0.9)), [])
        XCTAssertEqual(MenuInput.actions(from: GamepadState(dpadUp: true), to: idle), [])  // release does nothing
    }
}

final class SeatingCapacityTests: XCTestCase {
    func testLimitReleasesAndRefuses() {
        var seating = Seating()
        _ = seating.update(DeviceFrame(keys: [0, 124], pads: [1: GamepadState(south: true)]))
        XCTAssertEqual(seating.devices.compactMap { $0 }.count, 3)
        seating.limit(to: 2)
        XCTAssertNil(seating.seat(of: .controller(id: 1)))  // was seat 2
        let update = seating.update(DeviceFrame(keys: [0, 124], pads: [1: GamepadState(south: true)]))
        XCTAssertEqual(update.joined, [])  // full at two seats
        XCTAssertEqual(update.inputs.count, 4)
    }
}
