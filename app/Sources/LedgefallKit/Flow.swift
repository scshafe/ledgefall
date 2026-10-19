/// What a menu can ask for, from any device.
public enum MenuAction: Equatable {
    case up, down, left, right, confirm, back
    case pause  // the controller's menu button: pause, or resume
}

public enum Phase: Equatable {
    case setup
    case playing
    case paused
    case results(winner: Int?)
}

/// What the host must do after a menu action.
public enum FlowEffect: Equatable {
    case startMatch  // a new match with `players` and `stocks`
    case showDemo    // back to a bot-only match behind the setup screen
}

/// The screens and their rules: setup (players 2...4, stocks 1...5, default 3),
/// the match, pause, results. Pure, so it is tested without a window.
public struct Flow: Equatable {
    public static let playersRange = 2...4
    public static let stocksRange = 1...5
    public static let rows = 3  // players, stocks, start

    public private(set) var phase: Phase = .setup
    public private(set) var players = 2
    public private(set) var stocks = 3
    public private(set) var selectedRow = 2

    public init() {}

    @discardableResult
    public mutating func handle(_ action: MenuAction) -> FlowEffect? {
        switch phase {
        case .setup:
            switch action {
            case .up: selectedRow = max(selectedRow - 1, 0)
            case .down: selectedRow = min(selectedRow + 1, Flow.rows - 1)
            case .left: adjust(-1)
            case .right: adjust(1)
            case .confirm:
                if selectedRow == Flow.rows - 1 {
                    phase = .playing
                    return .startMatch
                }
                selectedRow += 1
            case .back: break
            }
        case .playing:
            if action == .back || action == .pause { phase = .paused }
        case .paused:
            switch action {
            case .confirm, .pause: phase = .playing
            case .back:
                phase = .setup
                return .showDemo
            default: break
            }
        case .results:
            switch action {
            case .confirm:
                phase = .playing
                return .startMatch
            case .back:
                phase = .setup
                return .showDemo
            default: break
            }
        }
        return nil
    }

    /// The match in progress ended.
    public mutating func matchOver(winner: Int?) {
        if phase == .playing { phase = .results(winner: winner) }
    }

    private mutating func adjust(_ delta: Int) {
        switch selectedRow {
        case 0: players = min(max(players + delta, Flow.playersRange.lowerBound), Flow.playersRange.upperBound)
        case 1: stocks = min(max(stocks + delta, Flow.stocksRange.lowerBound), Flow.stocksRange.upperBound)
        default: break
        }
    }
}

public enum MenuInput {
    /// A key press (not a repeat) as a menu action: Return, Enter and Space confirm, Esc goes back, arrows move.
    public static func action(forKey code: UInt16) -> MenuAction? {
        switch code {
        case 36, 76, 49: return .confirm
        case 53: return .back
        case 126: return .up
        case 125: return .down
        case 123: return .left
        case 124: return .right
        default: return nil
        }
    }

    /// Menu actions for a controller that moved from `old` to `new`: A confirms, B goes back, and
    /// the d-pad or the left stick moves, each on the change into a direction (no auto-repeat).
    public static func actions(from old: GamepadState, to new: GamepadState) -> [MenuAction] {
        var out: [MenuAction] = []
        if new.south && !old.south { out.append(.confirm) }
        if new.east && !old.east { out.append(.back) }
        if new.menu && !old.menu { out.append(.pause) }
        let (ox, oy) = direction(old), (nx, ny) = direction(new)
        if nx != ox && nx != 0 { out.append(nx < 0 ? .left : .right) }
        if ny != oy && ny != 0 { out.append(ny > 0 ? .up : .down) }
        return out
    }

    private static func direction(_ pad: GamepadState) -> (x: Int, y: Int) {
        let threshold: Float = 0.6
        var x = 0, y = 0
        if pad.dpadLeft || pad.leftX < -threshold { x = -1 }
        if pad.dpadRight || pad.leftX > threshold { x = 1 }
        if pad.dpadUp || pad.leftY > threshold { y = 1 }
        if pad.dpadDown || pad.leftY < -threshold { y = -1 }
        return (x, y)
    }
}
