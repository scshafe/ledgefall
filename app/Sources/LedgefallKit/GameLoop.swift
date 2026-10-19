/// Runs a match at a fixed 60 ticks a second against a real clock that the
/// host supplies, so a slow frame runs several ticks and a fast one none.
public final class GameLoop {
    public let match: Match
    private var accumulator: Double = 0
    private var last: Double?
    /// Never run more than this many ticks for one frame (after a stall, drop time rather than spiral).
    public static let maxTicksPerFrame = 5

    public init(match: Match) {
        self.match = match
    }

    /// Move the clock to `now` without running any tick (while paused).
    public func idle(to now: Double) {
        last = now
        accumulator = 0
    }

    /// Advance to `now` (seconds, any monotonic clock). `inputs` is asked once per tick.
    public func advance(to now: Double, inputs: () -> [PlayerInput]) {
        defer { last = now }
        guard let last else { return }
        accumulator += max(0, now - last)
        let dt = 1.0 / Double(Match.ticksPerSecond)
        var ran = 0
        while accumulator >= dt {
            accumulator -= dt
            if ran < GameLoop.maxTicksPerFrame, !match.isOver {
                match.step(inputs())
                ran += 1
            }
        }
    }
}
