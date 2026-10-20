/// What a menu can ask for, from any device.
public enum MenuAction: Equatable {
    case up, down, left, right, confirm, back
    case pause  // the controller's menu button: pause, or resume
}

public enum Phase: Equatable {
    case setup
    case playing
    case paused
    case results(winner: Int?)
    case browsing    // online: choosing among the open rooms
    case connecting  // online: opening or joining a room, and waiting for the match to start
    case replays     // choosing a saved replay
    case watching    // watching one
}

/// What the host must do after a menu action.
public enum FlowEffect: Equatable {
    case startMatch  // a new match with `players` and `stocks`
    case showDemo    // back to a bot-only match behind the setup screen
    case hostRoom    // online: make a room from the setup, take a seat in it and wait for the others
    case browseRooms // online: start listing the open rooms
    case joinRoom(String)  // online: take a seat in this room
    case leaveOnline // online: drop the room or the match; back to the setup screen
    case rematch     // online: the same setup again (the host opens a room, the others join it)
    case listReplays // read the saved replays into the list
    case watchReplay(Int) // watch the replay at this index of the list
    case stopWatching     // back to the setup screen
}

/// Where a match is played.
public enum PlayMode: Int, CaseIterable, Equatable {
    case local, host, join, replay

    public var name: String { ["Local, against bots and friends", "Online: host a room", "Online: join a room", "Watch a saved replay"][rawValue] }
}

/// How the seats are split into teams (a match needs two sides; teams of one seat each is "off").
public enum TeamMode: Int, CaseIterable, Equatable {
    case off, alternating, halves

    public var name: String { ["Free for all", "1 + 3 vs 2 + 4", "1 + 2 vs 3 + 4"][rawValue] }

    /// The team (0 or 1) of each of `players` seats; nil plays alone.
    public func teams(players: Int) -> [Int?] {
        switch self {
        case .off: return Array(repeating: nil, count: players)
        case .alternating: return (0..<players).map { $0 % 2 }
        case .halves: return (0..<players).map { $0 < (players + 1) / 2 ? 0 : 1 }
        }
    }
}

/// The screens and their rules: setup (players 2...4, stocks 1...5, default 3, the stage, teams and a
/// character for each seat), the match, pause, results. Pure, so it is tested without a window.
public struct Flow: Equatable {
    public static let playersRange = 2...4
    public static let stocksRange = 1...5

    // Setup rows: mode, people on this Mac (online), players, stocks, stage, teams, one per seat (the character), start.
    public static let modeRow = 0, localRow = 1, playersRow = 2, stocksRow = 3, stageRow = 4, teamsRow = 5, firstSeatRow = 6

    public private(set) var phase: Phase = .setup
    public private(set) var mode: PlayMode = .local
    /// An online match is on (or its results are up): the pause key leaves instead of pausing.
    public private(set) var online = false
    /// Online: the open rooms, the one chosen, and what the connecting screen says.
    public private(set) var rooms: [LobbyRoom] = []
    public private(set) var roomIndex = 0
    public private(set) var status = ""
    public private(set) var failed = false
    /// The saved replays (newest first), the one chosen, and whether the one being watched is paused.
    public private(set) var replays: [ReplayInfo] = []
    public private(set) var replayIndex = 0
    public private(set) var watchPaused = false
    public private(set) var players = 2
    /// Online: how many people play from this Mac, each in a seat of their own (1...4, never more than the room's seats).
    public private(set) var localPlayers = 1
    public private(set) var stocks = 3
    public private(set) var arena: Arena = .yard
    public private(set) var teamMode: TeamMode = .off
    public private(set) var fighters: [Fighter] = [.mason, .glim, .pip, .fuse]
    public private(set) var selectedRow = 8

    public init() {}

    /// The number of setup rows; the last is Start.
    public var rows: Int { Flow.firstSeatRow + players + 1 }
    public var startRow: Int { rows - 1 }

    @discardableResult
    public mutating func handle(_ action: MenuAction) -> FlowEffect? {
        switch phase {
        case .setup:
            switch action {
            case .up: selectedRow = max(selectedRow - 1, 0)
            case .down: selectedRow = min(selectedRow + 1, rows - 1)
            case .left: adjust(-1)
            case .right: adjust(1)
            case .confirm:
                if selectedRow == startRow {
                    switch mode {
                    case .local:
                        phase = .playing
                        return .startMatch
                    case .host:
                        phase = .connecting
                        status = "Opening a room..."
                        failed = false
                        return .hostRoom
                    case .join:
                        phase = .browsing
                        rooms = []
                        roomIndex = 0
                        return .browseRooms
                    case .replay:
                        phase = .replays
                        replays = []
                        replayIndex = 0
                        return .listReplays
                    }
                }
                selectedRow += 1
            case .back, .pause: break
            }
        case .playing:
            if action == .back || action == .pause {
                if online {
                    leaveOnline()
                    return .leaveOnline
                }
                phase = .paused
            }
        case .paused:
            switch action {
            case .confirm, .pause: phase = .playing
            case .back:
                phase = .setup
                return .showDemo
            default: break
            }
        case .results:
            switch action {
            case .confirm where online:
                phase = .connecting
                online = false
                status = "Waiting for a rematch..."
                failed = false
                return .rematch
            case .confirm:
                phase = .playing
                return .startMatch
            case .back where online:
                leaveOnline()
                return .leaveOnline
            case .back:
                phase = .setup
                return .showDemo
            default: break
            }
        case .browsing:
            switch action {
            case .up: roomIndex = max(roomIndex - 1, 0)
            case .down: roomIndex = max(min(roomIndex + 1, rooms.count - 1), 0)
            case .confirm:
                guard rooms.indices.contains(roomIndex) else { break }
                phase = .connecting
                status = "Taking a seat..."
                failed = false
                return .joinRoom(rooms[roomIndex].id)
            case .back:
                leaveOnline()
                return .leaveOnline
            default: break
            }
        case .replays:
            switch action {
            case .up: replayIndex = max(replayIndex - 1, 0)
            case .down: replayIndex = max(min(replayIndex + 1, replays.count - 1), 0)
            case .confirm:
                guard replays.indices.contains(replayIndex) else { break }
                phase = .watching
                watchPaused = false
                return .watchReplay(replayIndex)
            case .back:
                phase = .setup
                return .stopWatching
            default: break
            }
        case .watching:
            switch action {
            case .confirm, .pause: watchPaused.toggle()
            case .back:
                phase = .replays
                watchPaused = false
                return .listReplays
            default: break
            }
        case .connecting:
            if action == .back || (action == .confirm && failed) {
                leaveOnline()
                return .leaveOnline
            }
        }
        return nil
    }

    private mutating func leaveOnline() {
        phase = .setup
        online = false
        rooms = []
        status = ""
        failed = false
    }

    /// The match in progress ended.
    public mutating func matchOver(winner: Int?) {
        if phase == .playing { phase = .results(winner: winner) }
    }

    // MARK: online, told by the host as the network answers

    /// The open rooms, as the lobby lists them now (the chosen one stays chosen if it is still there).
    public mutating func setRooms(_ new: [LobbyRoom]) {
        guard phase == .browsing else { return }
        let chosen = rooms.indices.contains(roomIndex) ? rooms[roomIndex].id : nil
        rooms = new
        roomIndex = chosen.flatMap { id in new.firstIndex { $0.id == id } } ?? min(roomIndex, max(new.count - 1, 0))
    }

    /// The saved replays, read from disk when the list was asked for.
    public mutating func setReplays(_ new: [ReplayInfo]) {
        guard phase == .replays else { return }
        replays = new
        replayIndex = min(replayIndex, max(new.count - 1, 0))
    }

    /// A rematch room was found and joined, or the wait for it timed out and something is said instead.
    public mutating func setStatus(_ text: String) {
        guard phase == .connecting else { return }
        status = text
    }

    /// The online match has started.
    public mutating func onlineStarted() {
        guard phase == .connecting else { return }
        phase = .playing
        online = true
    }

    /// Something went wrong online; the screen says what and a key goes back.
    public mutating func onlineFailed(_ message: String) {
        guard phase == .connecting || phase == .browsing || (phase == .playing && online) else { return }
        phase = .connecting
        status = message
        failed = true
        online = false
    }

    private mutating func adjust(_ delta: Int) {
        func wrap(_ i: Int, _ n: Int) -> Int { ((i % n) + n) % n }
        switch selectedRow {
        case Flow.modeRow: mode = PlayMode(rawValue: wrap(mode.rawValue + delta, PlayMode.allCases.count)) ?? mode
        case Flow.localRow: localPlayers = min(max(localPlayers + delta, 1), min(players, 4))
        case Flow.playersRow:
            players = min(max(players + delta, Flow.playersRange.lowerBound), Flow.playersRange.upperBound)
            localPlayers = min(localPlayers, players)
        case Flow.stocksRow: stocks = min(max(stocks + delta, Flow.stocksRange.lowerBound), Flow.stocksRange.upperBound)
        case Flow.stageRow: arena = Arena(rawValue: wrap(arena.rawValue + delta, Arena.allCases.count)) ?? arena
        case Flow.teamsRow: teamMode = TeamMode(rawValue: wrap(teamMode.rawValue + delta, TeamMode.allCases.count)) ?? teamMode
        case Flow.firstSeatRow..<(Flow.firstSeatRow + players):
            let seat = selectedRow - Flow.firstSeatRow
            fighters[seat] = Fighter(rawValue: wrap(fighters[seat].rawValue + delta, Fighter.allCases.count)) ?? fighters[seat]
        default: break
        }
    }
}

public enum MenuInput {
    /// A key press (not a repeat) as a menu action: Return, Enter and Space confirm, Esc goes back, arrows move.
    public static func action(forKey code: UInt16) -> MenuAction? {
        switch code {
        case 36, 76, 49: return .confirm
        case 53: return .back
        case 126: return .up
        case 125: return .down
        case 123: return .left
        case 124: return .right
        default: return nil
        }
    }

    /// Menu actions for a controller that moved from `old` to `new`: A confirms, B goes back, and
    /// the d-pad or the left stick moves, each on the change into a direction (no auto-repeat).
    public static func actions(from old: GamepadState, to new: GamepadState) -> [MenuAction] {
        var out: [MenuAction] = []
        if new.south && !old.south { out.append(.confirm) }
        if new.east && !old.east { out.append(.back) }
        if new.menu && !old.menu { out.append(.pause) }
        let (ox, oy) = direction(old), (nx, ny) = direction(new)
        if nx != ox && nx != 0 { out.append(nx < 0 ? .left : .right) }
        if ny != oy && ny != 0 { out.append(ny > 0 ? .up : .down) }
        return out
    }

    private static func direction(_ pad: GamepadState) -> (x: Int, y: Int) {
        let threshold: Float = 0.6
        var x = 0, y = 0
        if pad.dpadLeft || pad.leftX < -threshold { x = -1 }
        if pad.dpadRight || pad.leftX > threshold { x = 1 }
        if pad.dpadUp || pad.leftY > threshold { y = 1 }
        if pad.dpadDown || pad.leftY < -threshold { y = -1 }
        return (x, y)
    }
}
