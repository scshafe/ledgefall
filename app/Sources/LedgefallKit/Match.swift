import CLedgefall

/// One tick of one seat's input.
public struct PlayerInput: Equatable {
    public var moveX: Int8  // -100...100
    public var jump: Bool
    public var down: Bool

    public init(moveX: Int8 = 0, jump: Bool = false, down: Bool = false) {
        self.moveX = moveX
        self.jump = jump
        self.down = down
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
    case solid = 0, shaking, gone, destroyed
}

public struct PlatformView {
    public var left: Float, right: Float, bottom: Float, top: Float
    public var phase: PlatformPhase
    public var timer: Int
}

public struct PlayerView {
    public var stocks: Int
    public var respawnTimer: Int
}

/// The Swift face of one match: owns the core's `lf_match`.
public final class Match {
    public static let ticksPerSecond = Int(LF_TICKS_PER_SECOND)
    static let fixedOne = Float(LF_FIXED_ONE)

    private let handle: OpaquePointer
    public let players: Int

    /// Nil unless `players` is 2...4 and `stocks` is 1...5.
    public init?(players: Int, stocks: Int = 3) {
        guard let h = lf_match_create(Int32(players), Int32(stocks)) else { return nil }
        handle = h
        self.players = players
    }

    deinit { lf_match_destroy(handle) }

    /// Make a seat a bot, or a human again with nil.
    @discardableResult
    public func setBot(seat: Int, level: BotLevel?) -> Bool {
        lf_match_set_bot(handle, Int32(seat), level?.rawValue ?? -1  // LF_BOT_NONE) != 0
    }

    /// Advance one tick; `inputs` has one entry per seat (a bot seat's entry is ignored).
    public func step(_ inputs: [PlayerInput]) {
        var raw = (0..<players).map { seat -> lf_input in
            let input = seat < inputs.count ? inputs[seat] : PlayerInput()
            var buttons: UInt8 = 0
            if input.jump { buttons |= UInt8(LF_BUTTON_JUMP) }
            if input.down { buttons |= UInt8(LF_BUTTON_DOWN) }
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
        guard lf_match_player(handle, Int32(seat), &p) != 0 else { return PlayerView(stocks: 0, respawnTimer: 0) }
        return PlayerView(stocks: Int(p.stocks), respawnTimer: Int(p.respawn_timer))
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
            phase: PlatformPhase(rawValue: p.phase) ?? .destroyed, timer: Int(p.timer))
    }

    /// The blast zone as (left, right, bottom, top).
    public var blast: (left: Float, right: Float, bottom: Float, top: Float) {
        var b: [Int32] = [0, 0, 0, 0]
        lf_match_blast(handle, &b)
        let one = Match.fixedOne
        return (Float(b[0]) / one, Float(b[1]) / one, Float(b[2]) / one, Float(b[3]) / one)
    }
}
