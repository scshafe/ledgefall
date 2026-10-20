import CLedgefall
import Foundation

/// One tick of one seat's input.
public struct PlayerInput: Equatable {
    public var moveX: Int8  // -100...100
    public var jump: Bool
    public var down: Bool
    public var place: Bool  // move slot 1
    public var trick: Bool  // move slot 2
    public var brk: Bool    // move slot 3 ("break" is a Swift keyword)

    public init(moveX: Int8 = 0, jump: Bool = false, down: Bool = false,
                place: Bool = false, trick: Bool = false, brk: Bool = false) {
        self.moveX = moveX
        self.jump = jump
        self.down = down
        self.place = place
        self.trick = trick
        self.brk = brk
    }
}

public enum BotLevel: Int32 {
    case easy = 0, normal = 1, hard = 2
}

/// A body, in world units (y up), converted from the core's fixed-point values.
public struct EntityView {
    public var x: Float, y: Float
    public var halfWidth: Float, height: Float
    public var facing: Int
    public var eliminated: Bool
}

public enum PlatformPhase: UInt8 {
    case solid = 0, shaking, gone, destroyed, doomed
}

public struct PlatformView {
    public var left: Float, right: Float, bottom: Float, top: Float
    public var phase: PlatformPhase
    public var timer: Int
    /// A sturdy slab (hit points, never crumbles by standing) rather than a crumble tile.
    public var sturdy: Bool
    public var hp: Int
    /// A tile's stress, 0...1.
    public var stress: Float
    /// Its conveyor speed right now (units a tick, + right); 0 for none.
    public var belt: Float
}

public enum ThingKind: Int {
    case wall = 0, spring, glue, oil, lob, acid, gust, anvil, bumper, charge, saw, thorns, roots, rub, magnet, comet, rock, cometWarning, rockWarning, ledge, decoy, halo, hollow, leaf, vine
}

/// A placeable or an object on the stage, as the box it is drawn in.
public struct ThingView {
    public var kind: ThingKind
    public var left: Float, right: Float, bottom: Float, top: Float
    public var owner: Int
    public var armed: Bool
}

/// The characters (fighters), in the core's order.
public enum Fighter: Int, CaseIterable {
    case mason, glim, pip, fuse, dab, fern, ferro

    public var name: String {
        ["Mason", "Glim", "Pip", "Fuse", "Dab", "Fern", "Ferro"][rawValue]
    }
}

/// The stages, in the core's order.
public enum Arena: Int, CaseIterable {
    case practice, yard, quarry, tide, gale, swell, belts

    public var name: String { ["Practice", "Crumble Yard", "The Quarry", "Tide Pools", "Gale Bridge", "Sinking Ship", "Conveyor Sky"][rawValue] }
}

public struct PlayerView {
    public var stocks: Int
    public var respawnTimer: Int
    public var character: Fighter
    /// Ticks until each of the three moves can be cast again.
    public var cooldown: [Int]
    public var place: Int
    /// -1 plays alone, else 0 or 1.
    public var team: Int
    public var kos: Int
    public var falls: Int
}

/// The Swift face of one match: owns the core's `lf_match`.
public final class Match {
    public static let ticksPerSecond = Int(LF_TICKS_PER_SECOND)
    static let fixedOne = Float(LF_FIXED_ONE)

    private let handle: OpaquePointer
    private let owns: Bool
    public let players: Int

    /// Nil unless `players` is 2...4 and `stocks` is 1...5 (and the rest is in range).
    /// `teams` gives each seat a team (0 or 1; nil plays alone; a match needs two sides, else all play alone),
    /// `seatStocks` a handicap (1...9 stocks for the seat instead of `stocks`; 0 for none).
    public convenience init?(players: Int, stocks: Int = 3, seed: UInt32 = 1, characters: [Fighter]? = nil,
                             arena: Arena = .practice, teams: [Int?]? = nil, seatStocks: [Int]? = nil) {
        var setup = lf_setup()
        setup.players = Int32(players)
        setup.stocks = Int32(stocks)
        setup.seed = seed
        setup.stage = Int32(arena.rawValue)
        let defaults: [Fighter] = [.mason, .glim, .pip, .fuse]
        let seats = [0, 1, 2, 3]
        let fighters = seats.map { i -> UInt8 in
            let f = (characters != nil && i < characters!.count && i < players) ? characters![i] : defaults[i]
            return UInt8(f.rawValue)
        }
        let teamValues = seats.map { i -> Int8 in
            guard let teams, i < teams.count, let t = teams[i] else { return -1 }
            return Int8(t)
        }
        let handicaps = seats.map { i -> Int8 in
            guard let seatStocks, i < seatStocks.count else { return 0 }
            return Int8(clamping: seatStocks[i])
        }
        setup.characters = (fighters[0], fighters[1], fighters[2], fighters[3])
        setup.teams = (teamValues[0], teamValues[1], teamValues[2], teamValues[3])
        setup.seat_stocks = (handicaps[0], handicaps[1], handicaps[2], handicaps[3])
        self.init(setup: setup)
    }

    init?(setup: lf_setup) {
        var setup = setup
        guard let h = lf_match_create_setup(&setup) else { return nil }
        self.handle = h
        self.owns = true
        self.players = Int(setup.players)
    }

    /// A read-only view of someone else's match (an online one): it can be drawn but not stepped, and
    /// it must not outlive the object it looks into.
    init(view: OpaquePointer) {
        self.handle = view
        self.owns = false
        self.players = Int(lf_match_players(view))
    }

    deinit { if owns { lf_match_destroy(handle) } }

    /// Make a seat a bot, or a human again with nil.
    @discardableResult
    public func setBot(seat: Int, level: BotLevel?) -> Bool {
        // -1 is LF_BOT_NONE.
        lf_match_set_bot(handle, Int32(seat), level?.rawValue ?? -1) != 0
    }

    /// Advance one tick; `inputs` has one entry per seat (a bot seat's entry is ignored).
    public func step(_ inputs: [PlayerInput]) {
        var raw = (0..<players).map { seat -> lf_input in
            let input = seat < inputs.count ? inputs[seat] : PlayerInput()
            var buttons: UInt8 = 0
            if input.jump { buttons |= UInt8(LF_BUTTON_JUMP) }
            if input.down { buttons |= UInt8(LF_BUTTON_DOWN) }
            if input.place { buttons |= UInt8(LF_BUTTON_PLACE) }
            if input.trick { buttons |= UInt8(LF_BUTTON_TRICK) }
            if input.brk { buttons |= UInt8(LF_BUTTON_BREAK) }
            return lf_input(move_x: input.moveX, buttons: buttons)
        }
        lf_match_step(handle, &raw)
    }

    /// The match so far as a replay: its setup and every seat's input on every tick (bots' included).
    public func replayData() -> Data? {
        let size = lf_match_replay_size(handle)
        var bytes = [UInt8](repeating: 0, count: size)
        guard size > 0, lf_match_replay_write(handle, &bytes, size) == size else { return nil }
        return Data(bytes)
    }

    public var tick: Int { Int(lf_match_tick(handle)) }
    public var isOver: Bool { lf_match_over(handle) != 0 }
    /// The winning seat once the match is over; nil for none.
    public var winner: Int? {
        let w = Int(lf_match_winner(handle))
        return w >= 0 ? w : nil
    }
    /// The team that won (0 or 1) in a team match once it is over; nil otherwise.
    public var winningTeam: Int? {
        let t = Int(lf_match_winning_team(handle))
        return t >= 0 ? t : nil
    }
    public var hash: UInt64 { lf_match_hash(handle) }

    public func player(_ seat: Int) -> PlayerView {
        var p = lf_player()
        guard lf_match_player(handle, Int32(seat), &p) != 0 else {
            return PlayerView(stocks: 0, respawnTimer: 0, character: .mason, cooldown: [0, 0, 0], place: 0, team: -1, kos: 0, falls: 0)
        }
        return PlayerView(
            stocks: Int(p.stocks), respawnTimer: Int(p.respawn_timer),
            character: Fighter(rawValue: Int(p.character)) ?? .mason,
            cooldown: [Int(p.cooldown.0), Int(p.cooldown.1), Int(p.cooldown.2)],
            place: Int(p.place), team: Int(p.team), kos: Int(p.kos), falls: Int(p.falls))
    }

    public func entity(_ seat: Int) -> EntityView? {
        var e = lf_entity()
        guard lf_match_entity(handle, Int32(seat), &e) != 0 else { return nil }
        return EntityView(
            x: Float(e.x) / Match.fixedOne, y: Float(e.y) / Match.fixedOne,
            halfWidth: Float(e.half_width) / Match.fixedOne, height: Float(e.height) / Match.fixedOne,
            facing: Int(e.facing), eliminated: e.eliminated != 0)
    }

    public var platformCount: Int { Int(lf_match_platform_count(handle)) }

    public func platform(_ index: Int) -> PlatformView? {
        var p = lf_platform()
        guard lf_match_platform(handle, Int32(index), &p) != 0 else { return nil }
        return PlatformView(
            left: Float(p.left) / Match.fixedOne, right: Float(p.right) / Match.fixedOne,
            bottom: Float(p.bottom) / Match.fixedOne, top: Float(p.top) / Match.fixedOne,
            phase: PlatformPhase(rawValue: p.phase) ?? .destroyed, timer: Int(p.timer),
            sturdy: p.sturdy != 0, hp: Int(p.hp), stress: Float(p.stress) / Match.fixedOne,
            belt: Float(p.belt) / Match.fixedOne)
    }

    /// The tide and the gale.
    public struct Weather {
        /// 0 calm, 1 warning, 2 rising, 3 holding, 4 falling
        public var tidePhase: Int
        public var tideLevel: Float
        /// 0 none, 1 warning, 2 blowing
        public var galePhase: Int
        public var galeDir: Int
        /// The conveyors have stopped for their warning.
        public var beltsStopped: Bool
        public var beltsSign: Int
    }

    public var weather: Weather {
        var w = lf_weather()
        lf_match_weather(handle, &w)
        return Weather(tidePhase: Int(w.tide_phase), tideLevel: Float(w.tide_level) / Match.fixedOne,
                       galePhase: Int(w.gale_phase), galeDir: Int(w.gale_dir),
                       beltsStopped: w.belts_phase != 0, beltsSign: Int(w.belts_sign))
    }

    public var thingCount: Int { Int(lf_match_thing_count(handle)) }

    public func thing(_ index: Int) -> ThingView? {
        var t = lf_thing()
        guard lf_match_thing(handle, Int32(index), &t) != 0, let kind = ThingKind(rawValue: Int(t.kind)) else { return nil }
        let one = Match.fixedOne
        return ThingView(
            kind: kind, left: Float(t.left) / one, right: Float(t.right) / one,
            bottom: Float(t.bottom) / one, top: Float(t.top) / one, owner: Int(t.owner), armed: t.armed != 0)
    }

    /// The blast zone as (left, right, bottom, top).
    public var blast: (left: Float, right: Float, bottom: Float, top: Float) {
        var b: [Int32] = [0, 0, 0, 0]
        lf_match_blast(handle, &b)
        let one = Match.fixedOne
        return (Float(b[0]) / one, Float(b[1]) / one, Float(b[2]) / one, Float(b[3]) / one)
    }
}

/// A recorded match to watch again: make a match from it and step that with `inputs(at:)` tick by tick.
public final class Replay {
    private let handle: OpaquePointer
    public let players: Int
    public let ticks: Int
    /// What the match was: stage, stocks, seed, characters, teams.
    public let setup: OnlineSetup

    /// Nil if the bytes are not a well-formed replay.
    public init?(data: Data) {
        let bytes = [UInt8](data)
        guard let h = lf_replay_open(bytes, bytes.count) else { return nil }
        handle = h
        var setup = lf_setup()
        lf_replay_setup(h, &setup)
        players = Int(setup.players)
        ticks = Int(lf_replay_ticks(h))
        let t = [setup.teams.0, setup.teams.1, setup.teams.2, setup.teams.3]
        let f = [setup.characters.0, setup.characters.1, setup.characters.2, setup.characters.3]
        self.setup = OnlineSetup(
            arena: Arena(rawValue: Int(setup.stage)) ?? .practice, stocks: Int(setup.stocks), seed: setup.seed,
            fighters: f.prefix(players).map { Fighter(rawValue: Int($0)) ?? .mason },
            teams: t.prefix(players).map { $0 >= 0 ? Int($0) : nil })
    }

    deinit { lf_replay_close(handle) }

    /// Whether running it from the start ends with the hash it was recorded with.
    public var isGenuine: Bool { lf_replay_verify(handle) != 0 }

    /// A fresh match set up as the recording began, with no bots: step it with `inputs(at:)`.
    public func makeMatch() -> Match? {
        var setup = lf_setup()
        lf_replay_setup(handle, &setup)
        return Match(setup: setup)
    }

    /// Every seat's input on `tick`; empty past the end.
    public func inputs(at tick: Int) -> [PlayerInput] {
        var raw = [lf_input](repeating: lf_input(), count: 4)
        guard tick >= 0, lf_replay_inputs(handle, UInt32(tick), &raw) != 0 else { return [] }
        return raw.prefix(players).map { r in
            PlayerInput(moveX: r.move_x, jump: r.buttons & UInt8(LF_BUTTON_JUMP) != 0, down: r.buttons & UInt8(LF_BUTTON_DOWN) != 0,
                        place: r.buttons & UInt8(LF_BUTTON_PLACE) != 0, trick: r.buttons & UInt8(LF_BUTTON_TRICK) != 0,
                        brk: r.buttons & UInt8(LF_BUTTON_BREAK) != 0)
        }
    }
}
