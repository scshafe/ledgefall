/// A coloured rectangle in world units (y up). The renderer draws a list of these.
public struct Quad: Equatable {
    public var left: Float, bottom: Float, right: Float, top: Float
    public var color: SIMD4<Float>

    public init(left: Float, bottom: Float, right: Float, top: Float, color: SIMD4<Float>) {
        self.left = left
        self.bottom = bottom
        self.right = right
        self.top = top
        self.color = color
    }
}

/// Per-seat colours (lost-footing: P1 red, P2 blue, P3 green, P4 gold).
public let seatColors: [SIMD4<Float>] = [
    SIMD4(0.90, 0.25, 0.25, 1), SIMD4(0.25, 0.45, 0.95, 1),
    SIMD4(0.25, 0.75, 0.40, 1), SIMD4(0.95, 0.75, 0.20, 1),
]

public enum Scene {
    static let background = SIMD4<Float>(0.07, 0.08, 0.12, 1)
    static let tile = SIMD4<Float>(0.62, 0.50, 0.38, 1)
    static let shakingTile = SIMD4<Float>(0.85, 0.42, 0.25, 1)

    /// The quads for the match as it stands, back to front: blast-zone floor
    /// line, platforms, bodies, stock pips.
    public static func quads(for match: Match) -> [Quad] {
        var out: [Quad] = []
        let blast = match.blast

        for i in 0..<match.platformCount {
            guard let p = match.platform(i) else { continue }
            switch p.phase {
            case .solid:
                out.append(Quad(left: p.left, bottom: p.bottom, right: p.right, top: p.top, color: tile))
            case .shaking:
                // A jitter that depends only on the tick, so the picture is a function of the state.
                let shake: Float = match.tick % 4 < 2 ? 0.6 : -0.6
                out.append(Quad(left: p.left + shake, bottom: p.bottom, right: p.right + shake, top: p.top, color: shakingTile))
            case .gone, .destroyed:
                break
            }
        }

        for seat in 0..<match.players {
            guard let e = match.entity(seat), !e.eliminated else { continue }
            out.append(Quad(left: e.x - e.halfWidth, bottom: e.y, right: e.x + e.halfWidth, top: e.y + e.height,
                            color: seatColors[seat % seatColors.count]))
        }

        // Stocks as pips along the top of the view, one row per seat.
        for seat in 0..<match.players {
            let stocks = match.player(seat).stocks
            for s in 0..<stocks {
                let left = blast.left + 8 + Float(s) * 8
                let top = blast.top - 8 - Float(seat) * 8
                out.append(Quad(left: left, bottom: top - 5, right: left + 5, top: top, color: seatColors[seat % seatColors.count]))
            }
        }
        return out
    }
}

/// Fits the blast zone into a view, letterboxed, as a scale and offset taking
/// world units to clip space (-1...1): clip = world * scale + offset.
public struct Camera: Equatable {
    public var scale: SIMD2<Float>
    public var offset: SIMD2<Float>

    public init(blast: (left: Float, right: Float, bottom: Float, top: Float), viewWidth: Float, viewHeight: Float) {
        let worldW = max(blast.right - blast.left, 1)
        let worldH = max(blast.top - blast.bottom, 1)
        // World units per clip unit on each axis, chosen so the whole blast zone fits.
        let aspect = max(viewWidth, 1) / max(viewHeight, 1)
        var halfW = worldW / 2
        var halfH = worldH / 2
        if halfW / halfH < aspect { halfW = halfH * aspect } else { halfH = halfW / aspect }
        scale = SIMD2(1 / halfW, 1 / halfH)
        let cx = (blast.left + blast.right) / 2
        let cy = (blast.bottom + blast.top) / 2
        offset = SIMD2(-cx * scale.x, -cy * scale.y)
    }
}
