/// A seat's keys, by macOS virtual key code (Carbon's kVK_* values, which are
/// the same on every Mac keyboard layout for these physical keys).
public struct KeyLayout: Equatable {
    public var left: UInt16, right: UInt16, jump: UInt16, down: UInt16

    public init(left: UInt16, right: UInt16, jump: UInt16, down: UInt16) {
        self.left = left
        self.right = right
        self.jump = jump
        self.down = down
    }

    /// Arrow keys: left 123, right 124, down 125, up 126.
    public static let arrows = KeyLayout(left: 123, right: 124, jump: 126, down: 125)
    /// WASD: A 0, S 1, D 2, W 13.
    public static let wasd = KeyLayout(left: 0, right: 2, jump: 13, down: 1)

    /// IJKL: J 38, L 37, I 34, K 40.
    public static let ijkl = KeyLayout(left: 38, right: 37, jump: 34, down: 40)

    /// The clusters a keyboard offers, one seat each (lost-footing's P1, P2 and P3 clusters).
    public static let clusters: [KeyLayout] = [.wasd, .arrows, .ijkl]

    /// Whether any of this cluster's keys is held.
    public func isActive(held: Set<UInt16>) -> Bool {
        held.contains(left) || held.contains(right) || held.contains(jump) || held.contains(down)
    }

    /// The input for a set of held keys. Both directions held cancel out.
    public func input(held: Set<UInt16>) -> PlayerInput {
        var axis: Int8 = 0
        if held.contains(left) { axis -= 100 }
        if held.contains(right) { axis += 100 }
        return PlayerInput(moveX: axis, jump: held.contains(jump), down: held.contains(down))
    }
}
