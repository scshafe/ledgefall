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
    @Published private(set) var localSeats: Set<Int> = []
    /// Watching a replay: ticks played and in all.
    @Published private(set) var watchTicks: (played: Int, total: Int)?
    /// The lobby's address: the LEDGEFALL_LOBBY environment variable if it is set, else the one saved from the setup screen
    /// (the `lobby` preference, which `defaults write ledgefall lobby <url>` also sets).
    @Published private(set) var lobbyAddress: String? = AppModel.configuredLobby()
    /// Whether LEDGEFALL_LOBBY is set, and so wins over the saved address.
    let lobbyFromEnvironment = AppModel.environmentLobby() != nil

    private(set) var loop: GameLoop
    /// The online match, once the game server has been found; its view is what is drawn while the match is on.
    /// One session per person playing from this Mac, each in a seat of its own.
    private var sessions: [OnlineSession] = []
    private var sessionStarted: Double?
    private var onlineTask: Task<Void, Never>?
    private var watch: ReplayWatch?
    /// Whether this online match's replay has been kept (or given up on).
    private var onlineReplayKept = false
    private var lastReplayTry = 0.0
    private let library = ReplayLibrary.standard
    /// The online match last played, for a rematch: its setup, and whether this Mac hosted the room.
    private var lastOnline: (setup: OnlineSetup, hosted: Bool)?
    private var onlineSeating: Seating = {
        Seating()  // limited to the people playing from this Mac when an online match is set up
    }()
    private var localCount = 1
    private var seating = Seating()
    private let controllers = Controllers()
    private var previousPads: [Int: GamepadState] = [:]
    /// Keys currently held, kept up to date by the game view.
    var heldKeys = Set<UInt16>()

    init() {
        loop = GameLoop(match: AppModel.demoMatch())
        if let stored = UserDefaults.standard.array(forKey: AppModel.keysDefault) as? [[Int]], let saved = KeyBindings(stored: stored) {
            flow.setBindings(saved)
            seating = newSeating()
        }
        flow.setLobby(AppModel.savedLobby() ?? "")
        publish()
    }

    private static let keysDefault = "keys"

    private func newSeating() -> Seating { Seating(layouts: flow.bindings.layouts) }

    /// What is drawn: the online match while one is on, else the local one.
    var displayMatch: Match { (flow.online ? sessions.first?.match : nil) ?? (flow.phase == .watching ? watch?.match : nil) ?? loop.match }

    private static func environmentLobby() -> String? {
        ProcessInfo.processInfo.environment["LEDGEFALL_LOBBY"].flatMap { $0.isEmpty ? nil : $0 }
    }

    private static func savedLobby() -> String? { UserDefaults.standard.string(forKey: "lobby").flatMap { $0.isEmpty ? nil : $0 } }

    private static func configuredLobby() -> String? { environmentLobby() ?? savedLobby() }

    private func lobbyClient() throws -> LobbyClient {
        guard let address = lobbyAddress else {
            throw LobbyError("No lobby address. Set it under Mode: Set the lobby address")
        }
        guard let url = LobbyAddress.url(address) else { throw LobbyError("The lobby address is not valid") }
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
        perform(effect)
    }

    private func perform(_ effect: FlowEffect) {
        switch effect {
        case .startMatch: startMatch()
        case .showDemo: loop = GameLoop(match: AppModel.demoMatch())
        case .hostRoom: hostRoom()
        case .browseRooms: browseRooms()
        case .joinRoom(let id): joinRoom(id)
        case .rematch: rematch()
        case .listReplays:
            watch = nil
            flow.setReplays(library?.list() ?? [])
        case .watchReplay(let index):
            if let info = flow.replays.indices.contains(index) ? flow.replays[index] : nil,
               let replay = library?.load(info), let w = ReplayWatch(replay) {
                watch = w
            } else {
                handle(.back)  // unreadable now: back to the list
            }
        case .stopWatching:
            watch = nil
            loop = GameLoop(match: AppModel.demoMatch())
        case .leaveOnline:
            dropOnline()
            loop = GameLoop(match: AppModel.demoMatch())
        case .saveLobby(let text):
            if text.isEmpty { UserDefaults.standard.removeObject(forKey: "lobby") } else { UserDefaults.standard.set(text, forKey: "lobby") }
            lobbyAddress = AppModel.configuredLobby()
        case .bindingsChanged:
            UserDefaults.standard.set(flow.bindings.stored, forKey: AppModel.keysDefault)
            seating.layouts = flow.bindings.layouts
            onlineSeating.layouts = flow.bindings.layouts
        }
        publish()
    }

    /// Text typed (or pasted) while the lobby address is being entered.
    func typed(_ text: String) {
        flow.type(text)
    }

    func backspace() {
        flow.backspace()
    }

    /// A key press from the window. Game keys are read from `heldKeys`; menus use presses.
    func keyPressed(_ code: UInt16) {
        if flow.phase == .lobby && code == 49 { return }  // a space is not part of an address, and is not "confirm" here
        if flow.capturingKey {  // the controls screen is waiting for a key
            if let effect = flow.capture(key: code) { perform(effect) }
            return
        }
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
        sessions = []
        sessionStarted = nil
        localSeats = []
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
        hostRoom(OnlineSetup(
            arena: flow.arena, stocks: flow.stocks, seed: UInt32(truncatingIfNeeded: Int(Date().timeIntervalSince1970 * 1000)),
            fighters: Array(flow.fighters.prefix(flow.players)), teams: flow.teamMode.teams(players: flow.players)))
    }

    private func hostRoom(_ setup: OnlineSetup) {
        lastOnline = (setup, true)
        let count = min(flow.localPlayers, setup.players)
        runOnline { [self] lobby in
            guard try await lobby.gameServers() > 0 else { throw LobbyError("No game server is registered with the lobby") }
            let room = try await lobby.createRoom(setup)
            try await takeSeatsAndWait(lobby, room: room, setup: setup, count: count)
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
        lastOnline = (setup, false)
        let count = flow.localPlayers
        runOnline { [self] lobby in try await takeSeatsAndWait(lobby, room: id, setup: setup, count: count) }
    }

    /// The same match again: whoever hosted opens a new room with the same setup (and a new seed); the others
    /// wait for it to appear in the lobby's list and sit down.
    private func rematch() {
        let previous = lastOnline
        dropOnline()
        guard let previous else {
            flow.onlineFailed("There is no match to play again")
            return
        }
        lastOnline = previous
        if previous.hosted {
            var setup = previous.setup
            setup.seed = UInt32(truncatingIfNeeded: Int(Date().timeIntervalSince1970 * 1000))
            hostRoom(setup)
            return
        }
        let count = min(localCount, previous.setup.players)
        runOnline { [self] lobby in
            for _ in 0..<60 {  // up to a minute
                try Task.checkCancellation()
                let match = try await lobby.openRooms().first { room in
                    room.setup.map { $0.arena == previous.setup.arena && $0.stocks == previous.setup.stocks &&
                        $0.fighters == previous.setup.fighters && $0.teams == previous.setup.teams } ?? false
                }
                if let match, let setup = match.setup {
                    await MainActor.run { self.lastOnline = (setup, false) }
                    try await takeSeatsAndWait(lobby, room: match.id, setup: setup, count: count)
                    return
                }
                try await Task.sleep(nanoseconds: 1_000_000_000)
            }
            throw LobbyError("Nobody opened a rematch")
        }
    }

    /// Sit down in a room (one seat for each person at this Mac), say we are ready, and wait for the lobby to hand
    /// the room to a game server. Each seat is its own connection to the game server.
    private func takeSeatsAndWait(_ lobby: LobbyClient, room: String, setup: OnlineSetup, count: Int) async throws {
        let free = try await lobby.room(room)
        guard free.seatCount - free.taken >= count else {
            throw LobbyError(count == 1 ? "The room is full" : "The room has no \(count) free seats")
        }
        let base = AppModel.playerName()
        var secrets: [String] = []
        for k in 0..<count {
            let secret = try await lobby.join(room, as: count == 1 ? base : "\(base) \(k + 1)")
            try await lobby.ready(room, secret: secret)
            secrets.append(secret)
        }
        await MainActor.run { self.localCount = count }
        while true {
            try Task.checkCancellation()
            var tickets: [(token: String, udp: String)] = []
            for secret in secrets {
                let me = try await lobby.me(room, secret: secret)
                if me.state == "finished" { throw LobbyError("The room is closed") }
                if me.state == "playing", let token = me.token, let udp = me.udp { tickets.append((token, udp)) }
            }
            if tickets.count == secrets.count {
                var opened: [OnlineSession] = []
                for ticket in tickets {
                    guard let s = OnlineSession(setup: setup, server: ticket.udp, room: room, token: ticket.token) else {
                        throw LobbyError("Could not reach the game server at \(ticket.udp)")
                    }
                    opened.append(s)
                }
                await MainActor.run {
                    self.onlineSeating = self.newSeating()
                    self.onlineSeating.limit(to: count)
                    self.sessions = opened
                    self.onlineReplayKept = false
                    self.sessionStarted = Date().timeIntervalSince1970
                    self.flow.setStatus("Connecting to the game server...")
                }
                return
            }
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
        case .browsing, .replays:
            loop.advance(to: now) { [] }  // the demo plays on behind the list
        case .watching:
            if let watch {
                watch.paused = flow.watchPaused
                watch.advance(to: now)
            }
        case .connecting:
            loop.advance(to: now) { [] }
            if !sessions.isEmpty {
                let states = sessions.map { $0.update() }
                if states.contains(where: { $0 == .failed || $0 == .ended }) {
                    flow.onlineFailed("The game server did not accept us")
                    sessions = []
                } else if states.allSatisfy({ $0 == .running }) {
                    flow.onlineStarted()
                    localSeats = Set(sessions.compactMap(\.seat))
                } else if let started = sessionStarted, Date().timeIntervalSince1970 - started > 30 {
                    flow.onlineFailed("The game server did not answer")
                    sessions = []
                }
            }
        case .playing where flow.online:
            if let first = sessions.first {
                // The first people to use a keyboard cluster or a controller get the seats, in order.
                let update = onlineSeating.update(DeviceFrame(keys: heldKeys, pads: controllers.states()))
                var state = OnlineSession.State.running
                for (i, session) in sessions.enumerated() {
                    session.setInput(i < update.inputs.count ? update.inputs[i] : PlayerInput())
                    let s = session.update()
                    if s == .failed || s == .ended { state = s }
                }
                localSeats = Set(sessions.compactMap(\.seat))
                if first.match.isOver {
                    flow.matchOver(winner: first.match.winner)
                } else if state == .failed || state == .ended {
                    flow.onlineFailed("Disconnected from the game server")
                }
            }
        case .setup, .controls, .lobby:
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
            var ended = false
            for session in sessions { ended = session.update() == .ended || ended }  // keep answering the server until we leave
            // The replay is certain once the session has confirmed every tick: keep it then, or give up when the match is closed.
            if !onlineReplayKept, let first = sessions.first, ended || now - lastReplayTry > 0.5 {
                lastReplayTry = now  // (checking a replay re-plays it, so not every frame)
                if saveReplay(of: first.match) || ended { onlineReplayKept = true }
            }
        case .paused, .results:
            loop.idle(to: now)  // no ticks run, but the clock stays current so resuming does not lurch
        }
        publish()
    }

    /// Keep the match just played in the replay library. Best effort.
    @discardableResult
    private func saveReplay(of match: Match) -> Bool {
        guard let data = match.replayData(), let library else { return false }
        do {
            try library.save(data)
            return true
        } catch {
            NSLog("ledgefall: could not save the replay: \(error)")
            return false
        }
    }

    private func publish() {
        let match = displayMatch
        let newWatch = flow.phase == .watching ? watch.map { (played: $0.match.tick, total: $0.replay.ticks) } : nil
        if newWatch?.played != watchTicks?.played || newWatch?.total != watchTicks?.total { watchTicks = newWatch }
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
