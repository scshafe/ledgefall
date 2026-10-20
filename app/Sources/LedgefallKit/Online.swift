import CLedgefall
import Foundation

/// What an online room is for: the same setup on the server and on every client. It is the room's options
/// (netplay-kit's lobby keeps them with the room and shows them to everyone who lists it or joins): stage,
/// stocks, seed, a character per seat, a team per seat (-1 for none).
public struct OnlineSetup: Equatable {
    public var arena: Arena
    public var stocks: Int
    public var seed: UInt32
    public var fighters: [Fighter]
    public var teams: [Int?]

    public var players: Int { fighters.count }

    public init(arena: Arena, stocks: Int, seed: UInt32, fighters: [Fighter], teams: [Int?]) {
        self.arena = arena
        self.stocks = stocks
        self.seed = seed
        self.fighters = fighters
        self.teams = teams.count == fighters.count ? teams : Array(repeating: nil, count: fighters.count)
    }

    /// A name for the room in the lobby's list (its limit is 40 characters).
    public var roomTitle: String { String("\(arena.name), \(stocks) stocks".prefix(40)) }

    /// Nil unless `options` are a ledgefall room's options, with everything in range.
    public init?(options o: [String: Any]) {
        guard let stage = o["stage"] as? Int, let arena = Arena(rawValue: stage),
              let stocks = o["stocks"] as? Int, Flow.stocksRange.contains(stocks),
              let seed = o["seed"] as? Int, (0...Int(UInt32.max)).contains(seed),
              let characters = o["characters"] as? [Int], Flow.playersRange.contains(characters.count),
              let teamCodes = o["teams"] as? [Int], teamCodes.count == characters.count else { return nil }
        let fighters = characters.compactMap(Fighter.init(rawValue:))
        guard fighters.count == characters.count, teamCodes.allSatisfy({ (-1...1).contains($0) }) else { return nil }
        self.init(arena: arena, stocks: stocks, seed: UInt32(seed), fighters: fighters, teams: teamCodes.map { $0 < 0 ? nil : $0 })
    }

    /// The room's options for the game server (what `make_netplay_simulation` reads).
    public var options: [String: Any] {
        ["stocks": stocks, "seed": Int(seed), "stage": arena.rawValue,
         "characters": fighters.map { $0.rawValue }, "teams": teams.map { $0 ?? -1 }]
    }

    var lfSetup: lf_setup {
        var s = lf_setup()
        s.players = Int32(players)
        s.stocks = Int32(stocks)
        s.seed = seed
        s.stage = Int32(arena.rawValue)
        let f = (0..<4).map { i in UInt8(i < fighters.count ? fighters[i].rawValue : 0) }
        let t = (0..<4).map { i in Int8(i < teams.count ? (teams[i] ?? -1) : -1) }
        s.characters = (f[0], f[1], f[2], f[3])
        s.teams = (t[0], t[1], t[2], t[3])
        return s
    }
}

// MARK: lobby

public struct LobbyError: Error, Equatable, CustomStringConvertible {
    public var description: String
    public init(_ description: String) { self.description = description }
}

/// The lobby's address as a person types it: "host", "host:port" or a full URL. https unless it says otherwise.
public enum LobbyAddress {
    public static let maxLength = 200

    /// The URL for `text`; nil if it is not an address a lobby could have.
    public static func url(_ text: String) -> URL? {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty, trimmed.count <= maxLength else { return nil }
        guard let url = URL(string: trimmed.contains("://") ? trimmed : "https://\(trimmed)"),
              let scheme = url.scheme?.lowercased(), scheme == "http" || scheme == "https",
              let host = url.host, !host.isEmpty else { return nil }
        return url
    }

    /// Whether a typed character can be part of an address (no spaces, no control or non-ASCII characters).
    public static func accepts(_ c: Character) -> Bool { c.isASCII && c.asciiValue.map { $0 > 0x20 && $0 < 0x7F } == true }
}

/// How the lobby client reaches the network: URLSession in the app, a script in tests.
public protocol HTTPTransport {
    func send(_ request: URLRequest) async throws -> (data: Data, status: Int)
}

extension URLSession: HTTPTransport {
    public func send(_ request: URLRequest) async throws -> (data: Data, status: Int) {
        let (data, response) = try await data(for: request)
        return (data, (response as? HTTPURLResponse)?.statusCode ?? 0)
    }
}

public struct LobbyRoom: Equatable {
    public var id: String
    public var name: String
    public var state: String  // "open", "starting", "playing", "finished"
    public var seatCount: Int
    public var taken: Int
    /// The setup in the room's options; nil for a room that is not a ledgefall room.
    public var setup: OnlineSetup? = nil
}

public struct LobbyMe: Equatable {
    public var state: String
    /// Once the match is on: the token for the game server and where it listens ("host:port").
    public var token: String?
    public var udp: String?
}

/// netplay-kit's lobby (HTTP + JSON), as the app needs it: find or make a room, take a seat, get ready,
/// learn where to play.
public final class LobbyClient {
    private let base: URL
    private let transport: HTTPTransport

    public init(base: URL, transport: HTTPTransport = URLSession.shared) {
        self.base = base
        self.transport = transport
    }

    private func call(_ method: String, _ path: String, body: [String: Any]? = nil, bearer: String? = nil,
                      expecting: Set<Int> = [200]) async throws -> [String: Any] {
        guard let url = URL(string: path, relativeTo: base) else { throw LobbyError("bad lobby address") }
        var request = URLRequest(url: url)
        request.httpMethod = method
        request.timeoutInterval = 10
        if let body {
            request.httpBody = try JSONSerialization.data(withJSONObject: body)
            request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        }
        if let bearer { request.setValue("Bearer \(bearer)", forHTTPHeaderField: "Authorization") }
        let (data, status) = try await transport.send(request)
        let object = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any] ?? [:]
        guard expecting.contains(status) else {
            throw LobbyError((object["error"] as? String) ?? "the lobby answered \(status)")
        }
        return object
    }

    private static func room(_ o: [String: Any]) -> LobbyRoom? {
        guard let id = o["id"] as? String, let name = o["name"] as? String, let state = o["state"] as? String else { return nil }
        return LobbyRoom(id: id, name: name, state: state, seatCount: o["seat_count"] as? Int ?? 0, taken: o["taken"] as? Int ?? 0,
                         setup: (o["options"] as? [String: Any]).flatMap(OnlineSetup.init(options:)))
    }

    /// How many game servers are registered (a room cannot start with none).
    public func gameServers() async throws -> Int {
        try await call("GET", "/api/status")["game_servers"] as? Int ?? 0
    }

    /// The rooms that are open and are ledgefall rooms.
    public func openRooms() async throws -> [LobbyRoom] {
        let all = try await call("GET", "/api/rooms")["rooms"] as? [[String: Any]] ?? []
        return all.compactMap(LobbyClient.room).filter { $0.state == "open" && $0.setup != nil }
    }

    public func room(_ id: String) async throws -> LobbyRoom {
        guard let r = LobbyClient.room(try await call("GET", "/api/rooms/\(id)")) else { throw LobbyError("unreadable room") }
        return r
    }

    /// Make a room for `setup` (one seat per fighter); returns its id.
    public func createRoom(_ setup: OnlineSetup) async throws -> String {
        let body: [String: Any] = ["name": setup.roomTitle, "game": "ledgefall", "seats": setup.players, "options": setup.options]
        guard let id = try await call("POST", "/api/rooms", body: body, expecting: [201])["id"] as? String else {
            throw LobbyError("no room id")
        }
        return id
    }

    /// Take a seat; returns the secret that proves it is yours.
    public func join(_ room: String, as name: String) async throws -> String {
        guard let secret = try await call("POST", "/api/rooms/\(room)/join", body: ["name": name])["secret"] as? String else {
            throw LobbyError("no seat secret")
        }
        return secret
    }

    public func ready(_ room: String, secret: String) async throws {
        _ = try await call("POST", "/api/rooms/\(room)/ready", body: ["ready": true], bearer: secret)
    }

    /// Where this seat stands; once the match is on, its token and the game server's address.
    public func me(_ room: String, secret: String) async throws -> LobbyMe {
        let o = try await call("POST", "/api/rooms/\(room)/me", bearer: secret)
        let server = o["server"] as? [String: Any]
        return LobbyMe(state: o["state"] as? String ?? "", token: o["token"] as? String, udp: server?["udp"] as? String)
    }
}

// MARK: playing

/// The client end of an online match: connects to the game server, plays the local input, and shows the match.
public final class OnlineSession {
    public enum State: Int {
        case connecting = 0, waiting, running, ended, failed
    }

    private let handle: OpaquePointer
    /// The match as this client has it (predicted); read-only.
    public let match: Match

    /// Nil if the setup, the address or the token is no good, or no socket can be opened.
    public init?(setup: OnlineSetup, server: String, room: String?, token: String?) {
        var s = setup.lfSetup
        guard let h = lf_net_open(&s, server, room, token), let view = lf_net_view(h) else { return nil }
        handle = h
        match = Match(view: view)
    }

    deinit { lf_net_close(handle) }

    public func setInput(_ input: PlayerInput) {
        var buttons: UInt8 = 0
        if input.jump { buttons |= UInt8(LF_BUTTON_JUMP) }
        if input.down { buttons |= UInt8(LF_BUTTON_DOWN) }
        if input.place { buttons |= UInt8(LF_BUTTON_PLACE) }
        if input.trick { buttons |= UInt8(LF_BUTTON_TRICK) }
        if input.brk { buttons |= UInt8(LF_BUTTON_BREAK) }
        lf_net_set_input(handle, lf_input(move_x: input.moveX, buttons: buttons))
    }

    /// Pump the connection; call once per frame.
    @discardableResult
    public func update() -> State { State(rawValue: Int(lf_net_update(handle))) ?? .failed }

    /// This client's seat once the match has started.
    public var seat: Int? {
        var st = lf_net_stats()
        lf_net_stats_get(handle, &st)
        return st.seat >= 0 ? Int(st.seat) : nil
    }

    public var inputDelay: Int {
        var st = lf_net_stats()
        lf_net_stats_get(handle, &st)
        return Int(st.input_delay)
    }
}
