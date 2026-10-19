import CLedgefall

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
}

public enum ThingKind: Int {
    case wall = 0, spring, glue, oil, lob, acid, gust, anvil, bumper, charge, saw, thorns, roots, rub, magnet, comet, rock, cometWarning, rockWarning
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
    case practice, yard, quarry, tide, gale

    public var name: String { ["Practice", "Crumble Yard", "The Quarry", "Tide Pools", "Gale Bridge"][rawValue] }
}

public struct PlayerView {
    public var stocks: Int
    public var respawnTimer: Int
    public var character: Fighter
    /// Ticks until each of the three moves can be cast again.
    public var cooldown: [Int]
    public var place: Int
    public var kos: Int
    public var falls: Int
}

/// The Swift face of one match: owns the core's `lf_match`.
public final class Match {
    public static let ticksPerSecond = Int(LF_TICKS_PER_SECOND)
    static let fixedOne = Float(LF_FIXED_ONE)

    private let handle: OpaquePointer
    public let players: Int

    /// Nil unless `players` is 2...4 and `stocks` is 1...5.
    public init?(players: Int, stocks: Int = 3, seed: UInt32 = 1, characters: [Fighter]? = nil,
                 arena: Arena = .practice) {
        let handle: OpaquePointer?
        if let characters, characters.count >= players {
            let raw = characters.prefix(players).map { UInt8($0.rawValue) }
            handle = lf_match_create_with(Int32(players), Int32(stocks), seed, raw, Int32(arena.rawValue))
        } else {
            handle = lf_match_create_with(Int32(players), Int32(stocks), seed, nil, Int32(arena.rawValue))
        }
        guard let h = handle else { return nil }
        self.handle = h
        self.players = players
    }

    deinit { lf_match_destroy(handle) }

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

    public var tick: Int { Int(lf_match_tick(handle)) }
    public var isOver: Bool { lf_match_over(handle) != 0 }
    /// The winning seat once the match is over; nil for none.
    public var winner: Int? {
        let w = Int(lf_match_winner(handle))
        return w >= 0 ? w : nil
    }
    public var hash: UInt64 { lf_match_hash(handle) }

    public func player(_ seat: Int) -> PlayerView {
        var p = lf_player()
        guard lf_match_player(handle, Int32(seat), &p) != 0 else {
            return PlayerView(stocks: 0, respawnTimer: 0, character: .mason, cooldown: [0, 0, 0], place: 0, kos: 0, falls: 0)
        }
        return PlayerView(
            stocks: Int(p.stocks), respawnTimer: Int(p.respawn_timer),
            character: Fighter(rawValue: Int(p.character)) ?? .mason,
            cooldown: [Int(p.cooldown.0), Int(p.cooldown.1), Int(p.cooldown.2)],
            place: Int(p.place), kos: Int(p.kos), falls: Int(p.falls))
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
            sturdy: p.sturdy != 0, hp: Int(p.hp), stress: Float(p.stress) / Match.fixedOne)
    }

    /// The tide and the gale.
    public struct Weather {
        /// 0 calm, 1 warning, 2 rising, 3 holding, 4 falling
        public var tidePhase: Int
        public var tideLevel: Float
        /// 0 none, 1 warning, 2 blowing
        public var galePhase: Int
        public var galeDir: Int
    }

    public var weather: Weather {
        var w = lf_weather()
        lf_match_weather(handle, &w)
        return Weather(tidePhase: Int(w.tide_phase), tideLevel: Float(w.tide_level) / Match.fixedOne,
                       galePhase: Int(w.gale_phase), galeDir: Int(w.gale_dir))
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
