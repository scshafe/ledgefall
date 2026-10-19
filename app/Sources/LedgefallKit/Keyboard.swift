/// A seat's keys, by macOS virtual key code (Carbon's kVK_* values, which are
/// the same on every Mac keyboard layout for these physical keys).
public struct KeyLayout: Equatable {
    public var left: UInt16, right: UInt16, jump: UInt16, down: UInt16
    public var place: UInt16, trick: UInt16, brk: UInt16

    public static let none: UInt16 = 0xFFFF  // a key code no key sends

    public init(left: UInt16, right: UInt16, jump: UInt16, down: UInt16,
                place: UInt16 = KeyLayout.none, trick: UInt16 = KeyLayout.none, brk: UInt16 = KeyLayout.none) {
        self.left = left
        self.right = right
        self.jump = jump
        self.down = down
        self.place = place
        self.trick = trick
        self.brk = brk
    }

    /// Arrow keys: left 123, right 124, down 125, up 126; moves on . (47), / (44) and right shift (60).
    public static let arrows = KeyLayout(left: 123, right: 124, jump: 126, down: 125, place: 47, trick: 44, brk: 60)
    /// WASD: A 0, S 1, D 2, W 13; moves on Q (12), E (14) and R (15).
    public static let wasd = KeyLayout(left: 0, right: 2, jump: 13, down: 1, place: 12, trick: 14, brk: 15)

    /// IJKL: J 38, L 37, I 34, K 40; moves on U (32), O (31) and P (35).
    public static let ijkl = KeyLayout(left: 38, right: 37, jump: 34, down: 40, place: 32, trick: 31, brk: 35)

    /// The clusters a keyboard offers, one seat each (lost-footing's P1, P2 and P3 clusters).
    public static let clusters: [KeyLayout] = [.wasd, .arrows, .ijkl]

    /// Whether any of this cluster's keys is held.
    public func isActive(held: Set<UInt16>) -> Bool {
        [left, right, jump, down, place, trick, brk].contains { held.contains($0) }
    }

    /// The input for a set of held keys. Both directions held cancel out.
    public func input(held: Set<UInt16>) -> PlayerInput {
        var axis: Int8 = 0
        if held.contains(left) { axis -= 100 }
        if held.contains(right) { axis += 100 }
        return PlayerInput(moveX: axis, jump: held.contains(jump), down: held.contains(down),
                           place: held.contains(place), trick: held.contains(trick), brk: held.contains(brk))
    }
}
