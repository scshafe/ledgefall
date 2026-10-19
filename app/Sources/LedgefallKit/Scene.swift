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
    static let doomedTile = SIMD4<Float>(0.85, 0.20, 0.15, 1)
    static let slab = SIMD4<Float>(0.55, 0.60, 0.68, 1)

    static func mix(_ a: SIMD4<Float>, _ b: SIMD4<Float>, _ t: Float) -> SIMD4<Float> {
        a + (b - a) * min(max(t, 0), 1)
    }

    static func color(of kind: ThingKind) -> SIMD4<Float> {
        switch kind {
        case .wall: return SIMD4(0.50, 0.50, 0.55, 1)
        case .spring: return SIMD4(0.35, 0.85, 0.45, 1)
        case .glue: return SIMD4(0.85, 0.80, 0.30, 0.9)
        case .oil: return SIMD4(0.25, 0.20, 0.45, 0.9)
        case .lob: return SIMD4(0.90, 0.90, 0.90, 1)
        case .acid: return SIMD4(0.55, 0.95, 0.20, 0.9)
        case .gust: return SIMD4(0.70, 0.85, 1.00, 0.25)
        case .anvil: return SIMD4(0.30, 0.30, 0.33, 1)
        case .bumper: return SIMD4(0.95, 0.45, 0.15, 1)
        case .charge: return SIMD4(0.95, 0.15, 0.15, 1)
        case .saw: return SIMD4(0.80, 0.82, 0.86, 1)
        case .thorns: return SIMD4(0.35, 0.55, 0.20, 0.9)
        case .roots: return SIMD4(0.45, 0.30, 0.15, 0.8)
        case .rub: return SIMD4(0.95, 0.95, 0.95, 0.35)
        case .magnet: return SIMD4(0.75, 0.25, 0.85, 1)
        case .comet: return SIMD4(1.00, 0.60, 0.20, 1)
        case .rock: return SIMD4(0.45, 0.40, 0.35, 1)
        case .cometWarning, .rockWarning: return SIMD4(1.00, 0.30, 0.20, 0.25)
        }
    }

    /// The quads for the match as it stands, back to front: blast-zone floor
    /// line, platforms, bodies, stock pips.
    public static func quads(for match: Match) -> [Quad] {
        var out: [Quad] = []
        let blast = match.blast

        // The sea and the wind sit behind the stage.
        let weather = match.weather
        if weather.tidePhase >= 1 {
            let top = weather.tidePhase == 1 ? blast.bottom + 2 : weather.tideLevel
            out.append(Quad(left: blast.left, bottom: blast.bottom, right: blast.right, top: max(top, blast.bottom + 1),
                            color: SIMD4(0.20, 0.55, 0.85, weather.tidePhase == 1 ? 0.5 : 0.35)))
        }
        if weather.galePhase >= 1 {
            let alpha: Float = weather.galePhase == 2 ? 0.14 : 0.07
            out.append(Quad(left: blast.left, bottom: blast.bottom, right: blast.right, top: blast.top, color: SIMD4(0.8, 0.9, 1.0, alpha)))
        }

        for i in 0..<match.platformCount {
            guard let p = match.platform(i) else { continue }
            switch p.phase {
            case .solid:
                let base = p.sturdy ? slab : tile
                out.append(Quad(left: p.left, bottom: p.bottom, right: p.right, top: p.top,
                                color: p.sturdy ? base * (0.55 + 0.45 * Float(p.hp) / 4) + SIMD4(0, 0, 0, 0.45) : mix(base, shakingTile, p.stress)))
            case .doomed:
                out.append(Quad(left: p.left, bottom: p.bottom, right: p.right, top: p.top, color: doomedTile))
            case .shaking:
                // A jitter that depends only on the tick, so the picture is a function of the state.
                let shake: Float = match.tick % 4 < 2 ? 0.6 : -0.6
                out.append(Quad(left: p.left + shake, bottom: p.bottom, right: p.right + shake, top: p.top, color: shakingTile))
            case .gone, .destroyed:
                break
            }
        }

        for i in 0..<match.thingCount {
            guard let t = match.thing(i) else { continue }
            var color = color(of: t.kind)
            if !t.armed { color.w *= 0.4 }  // still arming
            out.append(Quad(left: t.left, bottom: t.bottom, right: t.right, top: t.top, color: color))
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
