import CLedgefall
import Foundation

/// What an online room is for: the same setup on the server and on every client. The lobby does not show a
/// room's options to its players yet (a netplay-kit gap), so until it does the setup also travels in the room's
/// name, which every client can read: "lf1 S1 K3 D123456 C0123 T0101" (stage, stocks, seed, a character digit
/// per seat, a team per seat: 0, 1 or n for none). The server gets the same thing as the room's options.
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

    public var roomName: String {
        let c = fighters.map { String($0.rawValue) }.joined()
        let t = teams.map { $0.map(String.init) ?? "n" }.joined()
        return "lf1 S\(arena.rawValue) K\(stocks) D\(seed) C\(c) T\(t)"
    }

    /// Nil unless `name` is a name `roomName` produces, with everything in range.
    public init?(roomName name: String) {
        let parts = name.split(separator: " ").map(String.init)
        guard parts.count == 6, parts[0] == "lf1",
              let stage = parts[1].dropFirst().first.flatMap({ parts[1].hasPrefix("S") ? Int(String($0)) : nil }),
              parts[1].count == 2, let arena = Arena(rawValue: stage),
              parts[2].hasPrefix("K"), let stocks = Int(parts[2].dropFirst()), (1...5).contains(stocks),
              parts[3].hasPrefix("D"), let seed = UInt32(parts[3].dropFirst()),
              parts[4].hasPrefix("C"), parts[5].hasPrefix("T") else { return nil }
        let fighters = parts[4].dropFirst().map { Int(String($0)).flatMap(Fighter.init(rawValue:)) }
        let teams = parts[5].dropFirst().map { ch -> Int?? in
            switch ch {
            case "n": return .some(nil)
            case "0": return .some(0)
            case "1": return .some(1)
            default: return nil
            }
        }
        guard (2...4).contains(fighters.count), fighters.allSatisfy({ $0 != nil }),
              teams.count == fighters.count, teams.allSatisfy({ $0 != nil }) else { return nil }
        self.init(arena: arena, stocks: stocks, seed: seed, fighters: fighters.compactMap { $0 },
                  teams: teams.map { $0 ?? nil })
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
    /// The setup the name carries; nil for a room that is not a ledgefall room.
    public var setup: OnlineSetup? { OnlineSetup(roomName: name) }
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
        return LobbyRoom(id: id, name: name, state: state, seatCount: o["seat_count"] as? Int ?? 0, taken: o["taken"] as? Int ?? 0)
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
        let body: [String: Any] = ["name": setup.roomName, "game": "ledgefall", "seats": setup.players, "options": setup.options]
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
        match = Match(view: OpaquePointer(view))
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
