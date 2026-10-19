import Combine
import LedgefallKit

/// Ties the pieces together: the flow of screens, the current match, the
/// devices. The renderer calls `frame(now:)` once per drawn frame.
final class AppModel: ObservableObject {
    @Published private(set) var flow = Flow()
    /// Per seat of the current match: stocks left, and whether a person (not a bot) holds the seat.
    @Published private(set) var stocks: [Int] = []
    @Published private(set) var humans: [Bool] = []

    private(set) var loop: GameLoop
    private var seating = Seating()
    private let controllers = Controllers()
    private var previousPads: [Int: GamepadState] = [:]
    /// Keys currently held, kept up to date by the game view.
    var heldKeys = Set<UInt16>()

    init() {
        loop = GameLoop(match: AppModel.demoMatch())
        publish()
    }

    private static func demoMatch() -> Match {
        guard let match = Match(players: 4, stocks: 3) else { fatalError("could not create a match") }
        for seat in 0..<4 { match.setBot(seat: seat, level: .normal) }
        return match
    }

    // MARK: menus

    func handle(_ action: MenuAction) {
        guard let effect = flow.handle(action) else { return }
        switch effect {
        case .startMatch: startMatch()
        case .showDemo: loop = GameLoop(match: AppModel.demoMatch())
        }
        publish()
    }

    /// A key press from the window. Game keys are read from `heldKeys`; menus use presses.
    func keyPressed(_ code: UInt16) {
        guard let action = MenuInput.action(forKey: code) else { return }
        // While playing only Esc is a menu key; the arrows are the game's.
        if flow.phase == .playing && action != .back { return }
        handle(action)
    }

    private func startMatch() {
        guard let match = Match(players: flow.players, stocks: flow.stocks) else { return }
        for seat in 0..<flow.players { match.setBot(seat: seat, level: .normal) }
        seating.limit(to: flow.players)
        // Devices that kept their seats stay people.
        for (seat, device) in seating.devices.enumerated() where device != nil && seat < flow.players {
            match.setBot(seat: seat, level: nil)
        }
        loop = GameLoop(match: match)
    }

    // MARK: per frame

    func frame(now: Double) {
        let pads = controllers.states()
        for (id, pad) in pads {
            for action in MenuInput.actions(from: previousPads[id] ?? GamepadState(), to: pad) {
                // While playing only "back" (pause) is a menu action; the stick and buttons are the game's.
                if flow.phase != .playing || action == .back { handle(action) }
            }
        }
        previousPads = pads

        switch flow.phase {
        case .setup:
            if loop.match.isOver { loop = GameLoop(match: AppModel.demoMatch()) }
            loop.advance(to: now) { [] }  // the demo match plays itself
        case .playing:
            let match = loop.match
            loop.advance(to: now) { [unowned self] in
                let update = seating.update(DeviceFrame(keys: heldKeys, pads: controllers.states()))
                for seat in update.joined { match.setBot(seat: seat, level: nil) }
                for seat in update.left { match.setBot(seat: seat, level: .normal) }
                return update.inputs
            }
            if match.isOver { flow.matchOver(winner: match.winner) }
        case .paused, .results:
            loop.idle(to: now)  // no ticks run, but the clock stays current so resuming does not lurch
        }
        publish()
    }

    private func publish() {
        let match = loop.match
        let newStocks = (0..<match.players).map { match.player($0).stocks }
        let newHumans = (0..<match.players).map { seating.devices[$0] != nil && flow.phase != .setup }
        if newStocks != stocks { stocks = newStocks }
        if newHumans != humans { humans = newHumans }
    }
}
