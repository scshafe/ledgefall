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

/// What a key on a cluster does, in the order the controls screen lists them.
public enum KeyAction: Int, CaseIterable {
    case left, right, jump, down, place, trick, brk

    public var name: String { ["Left", "Right", "Jump", "Down", "Place", "Trick", "Break"][rawValue] }
}

extension KeyLayout {
    public subscript(action: KeyAction) -> UInt16 {
        get {
            switch action {
            case .left: return left
            case .right: return right
            case .jump: return jump
            case .down: return down
            case .place: return place
            case .trick: return trick
            case .brk: return brk
            }
        }
        set {
            switch action {
            case .left: left = newValue
            case .right: right = newValue
            case .jump: jump = newValue
            case .down: down = newValue
            case .place: place = newValue
            case .trick: trick = newValue
            case .brk: brk = newValue
            }
        }
    }
}

/// The three key clusters as the player has set them. A key belongs to one action of one cluster: giving
/// it to another action takes it from the one that had it, which gets the key the other one gave up.
public struct KeyBindings: Equatable {
    public static let standard = KeyBindings(layouts: KeyLayout.clusters)
    /// What each cluster starts as, for the screen to name them by.
    public static let clusterNames = ["WASD", "arrows", "IJKL"]

    public private(set) var layouts: [KeyLayout]

    public init(layouts: [KeyLayout]) { self.layouts = layouts }

    /// From what `stored` wrote down; nil unless it is a full, valid set (so a damaged preference falls back to the defaults).
    public init?(stored: [[Int]]) {
        guard stored.count == KeyLayout.clusters.count, stored.allSatisfy({ $0.count == KeyAction.allCases.count }) else { return nil }
        let all = stored.flatMap { $0 }
        guard all.allSatisfy({ (0..<Int(KeyLayout.none)).contains($0) }), Set(all).count == all.count else { return nil }
        layouts = stored.map { codes in
            var layout = KeyLayout.wasd
            for action in KeyAction.allCases { layout[action] = UInt16(codes[action.rawValue]) }
            return layout
        }
    }

    /// The key codes of each cluster, in `KeyAction` order, for the preferences.
    public var stored: [[Int]] { layouts.map { layout in KeyAction.allCases.map { Int(layout[$0]) } } }

    /// Give `code` to `action` of a cluster. False (nothing changes) for no key, or a cluster that does not exist.
    @discardableResult
    public mutating func bind(_ code: UInt16, to action: KeyAction, cluster: Int) -> Bool {
        guard layouts.indices.contains(cluster), code != KeyLayout.none else { return false }
        let old = layouts[cluster][action]
        guard old != code else { return true }
        for c in layouts.indices {
            for other in KeyAction.allCases where layouts[c][other] == code { layouts[c][other] = old }
        }
        layouts[cluster][action] = code
        return true
    }

    public mutating func reset() { layouts = KeyLayout.clusters }
}

/// What a key is called on the controls screen.
public enum KeyName {
    private static let names: [UInt16: String] = [
        0: "A", 1: "S", 2: "D", 3: "F", 4: "H", 5: "G", 6: "Z", 7: "X", 8: "C", 9: "V", 11: "B", 12: "Q", 13: "W", 14: "E",
        15: "R", 16: "Y", 17: "T", 18: "1", 19: "2", 20: "3", 21: "4", 22: "6", 23: "5", 24: "=", 25: "9", 26: "7", 27: "-",
        28: "8", 29: "0", 30: "]", 31: "O", 32: "U", 33: "[", 34: "I", 35: "P", 36: "Return", 37: "L", 38: "J", 39: "'",
        40: "K", 41: ";", 42: "\\", 43: ",", 44: "/", 45: "N", 46: "M", 47: ".", 48: "Tab", 49: "Space", 50: "`", 51: "Delete",
        53: "Esc", 54: "Right command", 55: "Left command", 56: "Left shift", 57: "Caps lock", 58: "Left option",
        59: "Left control", 60: "Right shift", 61: "Right option", 62: "Right control", 63: "Fn",
        65: "Keypad .", 67: "Keypad *", 69: "Keypad +", 75: "Keypad /", 76: "Enter", 78: "Keypad -", 81: "Keypad =",
        82: "Keypad 0", 83: "Keypad 1", 84: "Keypad 2", 85: "Keypad 3", 86: "Keypad 4", 87: "Keypad 5", 88: "Keypad 6",
        89: "Keypad 7", 91: "Keypad 8", 92: "Keypad 9", 123: "Left arrow", 124: "Right arrow", 125: "Down arrow", 126: "Up arrow",
    ]

    public static func label(_ code: UInt16) -> String { names[code] ?? "key \(code)" }
}
