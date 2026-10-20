import Combine
import Foundation
import LedgefallKit

/// Ties the pieces together: the flow of screens, the current match, the
/// devices. The renderer calls `frame(now:)` once per drawn frame.
final class AppModel: ObservableObject {
    @Published private(set) var flow = Flow()
    /// Per seat of the current match: stocks left, and whether a person (not a bot) holds the seat.
    @Published private(set) var stocks: [Int] = []
    @Published private(set) var humans: [Bool] = []
    /// Per seat: the fighter, and the ticks until each move can be cast again.
    @Published private(set) var kits: [SeatKit] = []
    /// Online: this client's seat, once the match is on.
    @Published private(set) var localSeat: Int?
    /// The lobby's address, from the LEDGEFALL_LOBBY environment variable or `defaults write <app> lobby <url>`.
    let lobbyAddress: String? = AppModel.configuredLobby()

    private(set) var loop: GameLoop
    /// The online match, once the game server has been found; its view is what is drawn while the match is on.
    private var session: OnlineSession?
    private var sessionStarted: Double?
    private var onlineTask: Task<Void, Never>?
    private var onlineSeating: Seating = {
        var s = Seating()
        s.limit(to: 1)  // one person plays online from this Mac
        return s
    }()
    private var seating = Seating()
    private let controllers = Controllers()
    private var previousPads: [Int: GamepadState] = [:]
    /// Keys currently held, kept up to date by the game view.
    var heldKeys = Set<UInt16>()

    init() {
        loop = GameLoop(match: AppModel.demoMatch())
        publish()
    }

    /// What is drawn: the online match while one is on, else the local one.
    var displayMatch: Match { (flow.online ? session?.match : nil) ?? loop.match }

    private static func configuredLobby() -> String? {
        let text = ProcessInfo.processInfo.environment["LEDGEFALL_LOBBY"] ?? UserDefaults.standard.string(forKey: "lobby")
        guard let text, !text.isEmpty else { return nil }
        return text
    }

    private func lobbyClient() throws -> LobbyClient {
        guard let address = lobbyAddress else {
            throw LobbyError("No lobby address. Set LEDGEFALL_LOBBY, or: defaults write ledgefall lobby <address>")
        }
        guard let url = URL(string: address.contains("://") ? address : "https://\(address)") else { throw LobbyError("The lobby address is not valid") }
        return LobbyClient(base: url)
    }

    private static func demoMatch() -> Match {
        guard let match = Match(players: 4, stocks: 3, arena: .yard) else { fatalError("could not create a match") }
        for seat in 0..<4 { match.setBot(seat: seat, level: .normal) }
        return match
    }

    // MARK: menus

    func handle(_ action: MenuAction) {
        guard let effect = flow.handle(action) else { return }
        switch effect {
        case .startMatch: startMatch()
        case .showDemo: loop = GameLoop(match: AppModel.demoMatch())
        case .hostRoom: hostRoom()
        case .browseRooms: browseRooms()
        case .joinRoom(let id): joinRoom(id)
        case .leaveOnline:
            dropOnline()
            loop = GameLoop(match: AppModel.demoMatch())
        }
        publish()
    }

    /// A key press from the window. Game keys are read from `heldKeys`; menus use presses.
    func keyPressed(_ code: UInt16) {
        guard let action = MenuInput.action(forKey: code) else { return }
        // While playing only Esc is a menu key; the arrows are the game's.
        if flow.phase == .playing && action != .back { return }
        handle(action)
    }

    private func startMatch() {
        guard let match = Match(
            players: flow.players, stocks: flow.stocks, seed: UInt32(truncatingIfNeeded: Int(Date().timeIntervalSince1970 * 1000)),
            characters: Array(flow.fighters.prefix(flow.players)), arena: flow.arena,
            teams: flow.teamMode.teams(players: flow.players)) else { return }
        for seat in 0..<flow.players { match.setBot(seat: seat, level: .normal) }
        seating.limit(to: flow.players)
        // Devices that kept their seats stay people.
        for (seat, device) in seating.devices.enumerated() where device != nil && seat < flow.players {
            match.setBot(seat: seat, level: nil)
        }
        loop = GameLoop(match: match)
    }

    // MARK: online

    private func dropOnline() {
        onlineTask?.cancel()
        onlineTask = nil
        session = nil
        sessionStarted = nil
        localSeat = nil
    }

    /// Run lobby work off the main thread; whatever it throws is shown on the connecting screen.
    private func runOnline(_ work: @escaping (LobbyClient) async throws -> Void) {
        onlineTask?.cancel()
        onlineTask = Task { [weak self] in
            guard let model = self else { return }
            do {
                try await work(try model.lobbyClient())
            } catch is CancellationError {
            } catch {
                await MainActor.run { model.flow.onlineFailed("\(error)") }
            }
        }
    }

    private func say(_ text: String) async { await MainActor.run { self.flow.setStatus(text) } }

    private func hostRoom() {
        let setup = OnlineSetup(
            arena: flow.arena, stocks: flow.stocks, seed: UInt32(truncatingIfNeeded: Int(Date().timeIntervalSince1970 * 1000)),
            fighters: Array(flow.fighters.prefix(flow.players)), teams: flow.teamMode.teams(players: flow.players))
        runOnline { [self] lobby in
            guard try await lobby.gameServers() > 0 else { throw LobbyError("No game server is registered with the lobby") }
            let room = try await lobby.createRoom(setup)
            try await takeSeatAndWait(lobby, room: room, setup: setup)
        }
    }

    private func browseRooms() {
        runOnline { [self] lobby in
            while true {
                let rooms = try await lobby.openRooms()
                await MainActor.run { self.flow.setRooms(rooms) }
                try await Task.sleep(nanoseconds: 1_000_000_000)
            }
        }
    }

    private func joinRoom(_ id: String) {
        guard let setup = flow.rooms.first(where: { $0.id == id })?.setup else { return }
        runOnline { [self] lobby in try await takeSeatAndWait(lobby, room: id, setup: setup) }
    }

    /// Sit down in a room, say we are ready, and wait for the lobby to hand the room to a game server.
    private func takeSeatAndWait(_ lobby: LobbyClient, room: String, setup: OnlineSetup) async throws {
        let secret = try await lobby.join(room, as: AppModel.playerName())
        try await lobby.ready(room, secret: secret)
        while true {
            try Task.checkCancellation()
            let me = try await lobby.me(room, secret: secret)
            if me.state == "playing", let token = me.token, let udp = me.udp {
                guard let opened = OnlineSession(setup: setup, server: udp, room: room, token: token) else {
                    throw LobbyError("Could not reach the game server at \(udp)")
                }
                await MainActor.run {
                    self.session = opened
                    self.sessionStarted = Date().timeIntervalSince1970
                    self.flow.setStatus("Connecting to the game server...")
                }
                return
            }
            if me.state == "finished" { throw LobbyError("The room is closed") }
            let r = try await lobby.room(room)
            await say("Waiting for the other players (\(r.taken) of \(r.seatCount))...")
            try await Task.sleep(nanoseconds: 500_000_000)
        }
    }

    private static func playerName() -> String {
        let name = ProcessInfo.processInfo.userName.filter { $0.isASCII && ($0.isLetter || $0.isNumber) }
        return name.isEmpty ? "player" : String(name.prefix(20))
    }

    // MARK: per frame

    func frame(now: Double) {
        let pads = controllers.states()
        for (id, pad) in pads {
            for action in MenuInput.actions(from: previousPads[id] ?? GamepadState(), to: pad) {
                // While playing only the menu button pauses; the stick and the face buttons are the game's.
                if flow.phase != .playing || action == .pause { handle(action) }
            }
        }
        previousPads = pads

        switch flow.phase {
        case .browsing:
            loop.advance(to: now) { [] }  // the demo plays on behind the list
        case .connecting:
            loop.advance(to: now) { [] }
            if let session {
                switch session.update() {
                case .running:
                    flow.onlineStarted()
                    localSeat = session.seat
                case .failed, .ended:
                    flow.onlineFailed("The game server did not accept us")
                    self.session = nil
                case .connecting, .waiting:
                    if let started = sessionStarted, Date().timeIntervalSince1970 - started > 30 {
                        flow.onlineFailed("The game server did not answer")
                        self.session = nil
                    }
                }
            }
        case .playing where flow.online:
            if let session {
                let update = onlineSeating.update(DeviceFrame(keys: heldKeys, pads: controllers.states()))
                session.setInput(update.inputs.first ?? PlayerInput())
                let state = session.update()
                localSeat = session.seat
                if session.match.isOver {
                    flow.matchOver(winner: session.match.winner)
                } else if state == .failed || state == .ended {
                    flow.onlineFailed("Disconnected from the game server")
                }
            }
        case .setup:
            if loop.match.isOver { loop = GameLoop(match: AppModel.demoMatch()) }
            loop.advance(to: now) { [] }  // the demo match plays itself
        case .playing:
            let match = loop.match
            loop.advance(to: now) { [unowned self] in
                let update = seating.update(DeviceFrame(keys: heldKeys, pads: controllers.states()))
                for seat in update.joined { match.setBot(seat: seat, level: nil) }
                for seat in update.left { match.setBot(seat: seat, level: .normal) }
                return update.inputs
            }
            if match.isOver {
                flow.matchOver(winner: match.winner)
                saveReplay(of: match)
            }
        case .results where flow.online:
            _ = session?.update()  // keep answering the server until we leave
        case .paused, .results:
            loop.idle(to: now)  // no ticks run, but the clock stays current so resuming does not lurch
        }
        publish()
    }

    /// Keep the match just played: ~/Library/Application Support/Ledgefall/Replays/<time>.lfreplay. Best effort.
    private func saveReplay(of match: Match) {
        guard let data = match.replayData(),
              let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first else { return }
        let folder = base.appendingPathComponent("Ledgefall/Replays", isDirectory: true)
        let stamp = Int(Date().timeIntervalSince1970)
        do {
            try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
            try data.write(to: folder.appendingPathComponent("\(stamp).lfreplay"))
        } catch {
            NSLog("ledgefall: could not save the replay: \(error)")
        }
    }

    private func publish() {
        let match = displayMatch
        let newStocks = (0..<match.players).map { match.player($0).stocks }
        let newHumans = (0..<match.players).map { flow.online || (seating.devices[$0] != nil && flow.phase != .setup) }
        if newStocks != stocks { stocks = newStocks }
        if newHumans != humans { humans = newHumans }
        let newKits = (0..<match.players).map { seat -> SeatKit in
            let p = match.player(seat)
            return SeatKit(fighter: p.character, cooldown: p.cooldown, team: p.team)
        }
        if newKits != kits { kits = newKits }
    }
}

struct SeatKit: Equatable {
    var fighter: Fighter
    var cooldown: [Int]
    var team: Int
}
