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
        // Every platform is solid at the start, plus a halo and a body for each of 2 players and 2 x 3 stock pips.
        XCTAssertEqual(quads.count, match.platformCount + 2 + 2 + 6)
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
        XCTAssertEqual(flow.mode, .local)
        XCTAssertEqual(flow.selectedRow, flow.startRow)
        go(&flow, to: Flow.playersRow)
        for _ in 0..<5 { flow.handle(.right) }
        XCTAssertEqual(flow.players, 4)
        XCTAssertEqual(flow.rows, Flow.firstSeatRow + 4 + 1)
        XCTAssertEqual(flow.startRow, 10)
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

    func testPeopleOnThisMac() {
        var flow = Flow()
        XCTAssertEqual(flow.localPlayers, 1)
        go(&flow, to: Flow.localRow)
        flow.handle(.right)
        XCTAssertEqual(flow.localPlayers, 2)
        flow.handle(.right)  // no more than the room has seats
        XCTAssertEqual(flow.localPlayers, 2)
        go(&flow, to: Flow.playersRow)
        flow.handle(.right)
        flow.handle(.right)
        go(&flow, to: Flow.localRow)
        for _ in 0..<5 { flow.handle(.right) }
        XCTAssertEqual(flow.localPlayers, 4)
        go(&flow, to: Flow.playersRow)
        for _ in 0..<5 { flow.handle(.left) }  // fewer seats: fewer people here
        XCTAssertEqual(flow.players, 2)
        XCTAssertEqual(flow.localPlayers, 2)
    }

    func testOnlineHostingFlow() {
        var flow = Flow()
        go(&flow, to: Flow.modeRow)
        flow.handle(.right)
        XCTAssertEqual(flow.mode, .host)
        go(&flow, to: flow.startRow)
        XCTAssertEqual(flow.handle(.confirm), .hostRoom)
        XCTAssertEqual(flow.phase, .connecting)
        flow.setStatus("Waiting for the others...")
        XCTAssertEqual(flow.status, "Waiting for the others...")
        flow.onlineStarted()
        XCTAssertEqual(flow.phase, .playing)
        XCTAssertTrue(flow.online)
        // Online there is no pause: the key leaves.
        XCTAssertEqual(flow.handle(.pause), .leaveOnline)
        XCTAssertEqual(flow.phase, .setup)
        XCTAssertFalse(flow.online)
    }

    func testOnlineJoiningFlow() {
        var flow = Flow()
        go(&flow, to: Flow.modeRow)
        flow.handle(.right)
        flow.handle(.right)
        XCTAssertEqual(flow.mode, .join)
        go(&flow, to: flow.startRow)
        XCTAssertEqual(flow.handle(.confirm), .browseRooms)
        XCTAssertEqual(flow.phase, .browsing)
        XCTAssertNil(flow.handle(.confirm))  // no rooms yet: nothing to join
        let setup = OnlineSetup(arena: .yard, stocks: 3, seed: 1, fighters: [.mason, .glim], teams: [nil, nil])
        let a = LobbyRoom(id: "a", name: setup.roomName, state: "open", seatCount: 2, taken: 1)
        let b = LobbyRoom(id: "b", name: setup.roomName, state: "open", seatCount: 2, taken: 0)
        flow.setRooms([a, b])
        flow.handle(.down)
        flow.handle(.down)  // clamped
        XCTAssertEqual(flow.roomIndex, 1)
        // The list changes under the cursor: the chosen room stays chosen.
        flow.setRooms([b])
        XCTAssertEqual(flow.rooms.first?.id, "b")
        XCTAssertEqual(flow.roomIndex, 0)
        XCTAssertEqual(flow.handle(.confirm), .joinRoom("b"))
        XCTAssertEqual(flow.phase, .connecting)
        flow.onlineFailed("started")
        XCTAssertTrue(flow.failed)
        XCTAssertEqual(flow.status, "started")
        XCTAssertEqual(flow.handle(.confirm), .leaveOnline)  // any way out goes back to the setup
        XCTAssertEqual(flow.phase, .setup)
    }

    func testOnlineResultsOfferARematch() {
        var flow = Flow()
        go(&flow, to: Flow.modeRow)
        flow.handle(.right)
        go(&flow, to: flow.startRow)
        flow.handle(.confirm)
        flow.onlineStarted()
        flow.matchOver(winner: 1)
        XCTAssertEqual(flow.phase, .results(winner: 1))
        XCTAssertEqual(flow.handle(.confirm), .rematch)  // the same match again
        XCTAssertEqual(flow.phase, .connecting)
        XCTAssertEqual(flow.status, "Waiting for a rematch...")
        flow.onlineStarted()
        XCTAssertTrue(flow.online)
        flow.matchOver(winner: nil)
        XCTAssertEqual(flow.handle(.back), .leaveOnline)  // or leave
        XCTAssertEqual(flow.phase, .setup)
        // Backing out while connecting also leaves.
        flow.handle(.confirm)
        XCTAssertEqual(flow.phase, .connecting)
        XCTAssertEqual(flow.handle(.back), .leaveOnline)
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

final class OnlineSetupTests: XCTestCase {
    func testRoomNameRoundTrips() throws {
        let setup = OnlineSetup(arena: .tide, stocks: 4, seed: 4_000_000_001, fighters: [.ferro, .mason, .dab], teams: [0, nil, 1])
        XCTAssertEqual(setup.roomName, "lf1 S3 K4 D4000000001 C604 T0n1")
        XCTAssertLessThanOrEqual(setup.roomName.count, 40)  // the lobby's limit for a room name
        XCTAssertEqual(OnlineSetup(roomName: setup.roomName), setup)
        let widest = OnlineSetup(arena: .belts, stocks: 5, seed: UInt32.max, fighters: [.ferro, .ferro, .ferro, .ferro], teams: [0, 1, 0, 1])
        XCTAssertLessThanOrEqual(widest.roomName.count, 40)
        XCTAssertEqual(OnlineSetup(roomName: widest.roomName), widest)
    }

    func testRoomNamesThatAreNotOurs() {
        for name in ["", "my game", "lf1 S1 K3 D1 C0 T0", "lf1 S9 K3 D1 C01 Tnn", "lf1 S1 K0 D1 C01 Tnn", "lf1 S1 K3 D1 C01 Tn",
                     "lf1 S1 K3 D1 C09 Tnn", "lf1 S1 K3 D-1 C01 Tnn", "lf1 S1 K3 D1 C01 Tn2", "lf2 S1 K3 D1 C01 Tnn", "lf1 S1 K3 D1 C01234 Tnnnnn"] {
            XCTAssertNil(OnlineSetup(roomName: name), name)
        }
    }

    func testOptionsForTheServer() throws {
        let setup = OnlineSetup(arena: .yard, stocks: 2, seed: 7, fighters: [.pip, .glim], teams: [nil, nil])
        let o = setup.options
        XCTAssertEqual(o["stocks"] as? Int, 2)
        XCTAssertEqual(o["stage"] as? Int, 1)
        XCTAssertEqual(o["characters"] as? [Int], [2, 1])
        XCTAssertEqual(o["teams"] as? [Int], [-1, -1])
        XCTAssertTrue(JSONSerialization.isValidJSONObject(o))
    }
}

/// A scripted network: answers by method and path, and remembers what it was asked.
final class ScriptedTransport: HTTPTransport {
    var answers: [String: (Int, String)] = [:]
    var seen: [(method: String, path: String, body: [String: Any]?, bearer: String?)] = []

    func send(_ request: URLRequest) async throws -> (data: Data, status: Int) {
        let key = "\(request.httpMethod ?? "") \(request.url?.path ?? "")"
        let body = request.httpBody.flatMap { try? JSONSerialization.jsonObject(with: $0) as? [String: Any] }
        let bearer = request.value(forHTTPHeaderField: "Authorization")?.replacingOccurrences(of: "Bearer ", with: "")
        seen.append((request.httpMethod ?? "", request.url?.path ?? "", body, bearer))
        guard let (status, json) = answers[key] else { return (Data("{\"error\":\"nothing here\"}".utf8), 404) }
        return (Data(json.utf8), status)
    }
}

final class LobbyClientTests: XCTestCase {
    private let setup = OnlineSetup(arena: .yard, stocks: 3, seed: 5, fighters: [.mason, .glim], teams: [nil, nil])

    private func lobby(_ t: ScriptedTransport) -> LobbyClient { LobbyClient(base: URL(string: "http://127.0.0.1:8080")!, transport: t) }

    func testHostingARoomAndWaitingForTheMatch() async throws {
        let t = ScriptedTransport()
        t.answers["POST /api/rooms"] = (201, "{\"id\":\"r1\"}")
        t.answers["POST /api/rooms/r1/join"] = (200, "{\"secret\":\"s3cret\"}")
        t.answers["POST /api/rooms/r1/ready"] = (200, "{}")
        t.answers["POST /api/rooms/r1/me"] = (200, "{\"state\":\"playing\",\"token\":\"00112233445566778899aabbccddeeff\",\"server\":{\"udp\":\"ledgefall.example:7777\"}}")
        let client = lobby(t)
        let room = try await client.createRoom(setup)
        XCTAssertEqual(room, "r1")
        let secret = try await client.join(room, as: "Cole")
        try await client.ready(room, secret: secret)
        let me = try await client.me(room, secret: secret)
        XCTAssertEqual(me, LobbyMe(state: "playing", token: "00112233445566778899aabbccddeeff", udp: "ledgefall.example:7777"))

        // What was sent: the game, the seat count, the setup in the name and as options; the secret as a bearer.
        XCTAssertEqual(t.seen[0].body?["game"] as? String, "ledgefall")
        XCTAssertEqual(t.seen[0].body?["seats"] as? Int, 2)
        XCTAssertEqual(t.seen[0].body?["name"] as? String, setup.roomName)
        XCTAssertEqual((t.seen[0].body?["options"] as? [String: Any])?["stage"] as? Int, 1)
        XCTAssertEqual(t.seen[1].body?["name"] as? String, "Cole")
        XCTAssertNil(t.seen[1].bearer)
        XCTAssertEqual(t.seen[2].bearer, "s3cret")
        XCTAssertEqual(t.seen[3].bearer, "s3cret")
    }

    func testBrowsingOnlyLedgefallRoomsThatAreOpen() async throws {
        let t = ScriptedTransport()
        t.answers["GET /api/rooms"] = (200, """
            {"rooms":[
              {"id":"a","name":"\(setup.roomName)","state":"open","seat_count":2,"taken":1},
              {"id":"b","name":"\(setup.roomName)","state":"playing","seat_count":2,"taken":2},
              {"id":"c","name":"someone's toy game","state":"open","seat_count":2,"taken":0}]}
            """)
        t.answers["GET /api/status"] = (200, "{\"rooms\":3,\"game_servers\":1}")
        let client = lobby(t)
        let rooms = try await client.openRooms()
        XCTAssertEqual(rooms.map(\.id), ["a"])
        XCTAssertEqual(rooms.first?.setup, setup)
        XCTAssertEqual(rooms.first?.taken, 1)
        let servers = try await client.gameServers()
        XCTAssertEqual(servers, 1)
    }

    func testErrorsFromTheLobby() async throws {
        let t = ScriptedTransport()
        t.answers["POST /api/rooms/r/join"] = (409, "{\"error\":\"started\"}")
        let client = lobby(t)
        do {
            _ = try await client.join("r", as: "x")
            XCTFail("expected an error")
        } catch let e as LobbyError {
            XCTAssertEqual(e.description, "started")
        }
        do {
            _ = try await client.openRooms()
            XCTFail("expected an error")
        } catch let e as LobbyError {
            XCTAssertEqual(e.description, "nothing here")
        }
    }
}

final class OnlineSessionTests: XCTestCase {
    func testBadArgumentsAreRefused() {
        let setup = OnlineSetup(arena: .yard, stocks: 3, seed: 1, fighters: [.mason, .glim], teams: [nil, nil])
        XCTAssertNil(OnlineSession(setup: setup, server: "not an address", room: nil, token: nil))
        XCTAssertNil(OnlineSession(setup: setup, server: "127.0.0.1:7777", room: "r", token: "short"))
    }

    func testAnUnansweredServerKeepsConnecting() throws {
        let setup = OnlineSetup(arena: .yard, stocks: 3, seed: 1, fighters: [.mason, .glim], teams: [nil, nil])
        // Nothing listens on this port: the session just keeps trying, and its match view is a fresh match.
        let session = try XCTUnwrap(OnlineSession(setup: setup, server: "127.0.0.1:9", room: nil, token: nil))
        XCTAssertEqual(session.update(), .connecting)
        XCTAssertNil(session.seat)
        XCTAssertEqual(session.match.players, 2)
        XCTAssertEqual(session.match.tick, 0)
        XCTAssertFalse(session.match.isOver)
        session.setInput(PlayerInput(moveX: 100, jump: true))
        session.match.step([])  // a view cannot be stepped
        XCTAssertEqual(session.match.tick, 0)
    }
}


final class ReplayLibraryTests: XCTestCase {
    private func folder() throws -> URL {
        let url = FileManager.default.temporaryDirectory.appendingPathComponent("ledgefall-replays-\(UUID().uuidString)")
        addTeardownBlock { try? FileManager.default.removeItem(at: url) }
        return url
    }

    private func playedMatch(seed: UInt32) throws -> Match {
        let match = try XCTUnwrap(Match(players: 2, stocks: 1, seed: seed, characters: [.fuse, .pip], arena: .yard))
        for seat in 0..<2 { match.setBot(seat: seat, level: .hard) }
        var ticks = 0
        while !match.isOver && ticks < 6 * 60 * 60 {
            match.step([])
            ticks += 1
        }
        XCTAssertTrue(match.isOver)
        return match
    }

    func testSaveListAndLoad() throws {
        let library = ReplayLibrary(folder: try folder())
        XCTAssertEqual(library.list(), [])  // no folder yet
        let a = try XCTUnwrap(try playedMatch(seed: 1).replayData())
        let b = try XCTUnwrap(try playedMatch(seed: 2).replayData())
        let first = try library.save(a, at: Date(timeIntervalSince1970: 1_000))
        try FileManager.default.setAttributes([.modificationDate: Date(timeIntervalSince1970: 1_000)], ofItemAtPath: first.path)
        let second = try library.save(b, at: Date(timeIntervalSince1970: 1_000))  // the same second: a distinct file
        XCTAssertNotEqual(first, second)
        try FileManager.default.setAttributes([.modificationDate: Date(timeIntervalSince1970: 2_000)], ofItemAtPath: second.path)
        try Data("not a replay".utf8).write(to: library.folder.appendingPathComponent("junk.lfreplay"))
        try Data("x".utf8).write(to: library.folder.appendingPathComponent("notes.txt"))

        let list = library.list()
        XCTAssertEqual(list.count, 2)  // the junk and the text file are skipped
        XCTAssertEqual(list.first?.file, second.lastPathComponent)  // newest first
        XCTAssertEqual(list.first?.setup.arena, .yard)
        XCTAssertEqual(list.first?.setup.fighters, [.fuse, .pip])
        XCTAssertTrue(list.first?.title.hasPrefix("Crumble Yard, 1 stocks, Fuse / Pip, ") ?? false)
        let replay = try XCTUnwrap(library.load(list[0]))
        XCTAssertTrue(replay.isGenuine)
    }

    func testWatchingPlaysTheMatchBackToTheSameEnd() throws {
        let match = try playedMatch(seed: 5)
        let replay = try XCTUnwrap(Replay(data: try XCTUnwrap(match.replayData())))
        let watch = try XCTUnwrap(ReplayWatch(replay))
        XCTAssertEqual(watch.progress, 0)
        var now = 100.0
        watch.advance(to: now)  // sets the clock
        watch.paused = true
        now += 2
        watch.advance(to: now)
        XCTAssertEqual(watch.match.tick, 0)  // paused: nothing runs
        watch.paused = false
        var guardTicks = 0
        while !watch.finished && guardTicks < 100_000 {
            now += 1.0 / 60 * 3  // three ticks a frame
            watch.advance(to: now)
            guardTicks += 1
        }
        XCTAssertTrue(watch.finished)
        XCTAssertEqual(watch.match.tick, replay.ticks)
        XCTAssertEqual(watch.match.hash, match.hash)
        XCTAssertEqual(watch.progress, 1)
    }
}

extension FlowTests {
    func testReplayBrowsingFlow() {
        var flow = Flow()
        go(&flow, to: Flow.modeRow)
        flow.handle(.left)  // local -> replay (wraps round)
        XCTAssertEqual(flow.mode, .replay)
        go(&flow, to: flow.startRow)
        XCTAssertEqual(flow.handle(.confirm), .listReplays)
        XCTAssertEqual(flow.phase, .replays)
        XCTAssertNil(flow.handle(.confirm))  // nothing saved yet
        let setup = OnlineSetup(arena: .quarry, stocks: 3, seed: 1, fighters: [.mason, .glim], teams: [nil, nil])
        let infos = (0..<3).map { ReplayInfo(file: "\($0).lfreplay", date: Date(timeIntervalSince1970: Double($0)), setup: setup, ticks: 600 * ($0 + 1)) }
        flow.setReplays(infos)
        flow.handle(.down)
        XCTAssertEqual(flow.handle(.confirm), .watchReplay(1))
        XCTAssertEqual(flow.phase, .watching)
        flow.handle(.confirm)
        XCTAssertTrue(flow.watchPaused)
        flow.handle(.pause)
        XCTAssertFalse(flow.watchPaused)
        XCTAssertEqual(flow.handle(.back), .listReplays)  // back to the list
        XCTAssertEqual(flow.phase, .replays)
        XCTAssertEqual(flow.handle(.back), .stopWatching)
        XCTAssertEqual(flow.phase, .setup)
    }
}
