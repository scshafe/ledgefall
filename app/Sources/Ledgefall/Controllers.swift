import GameController
import LedgefallKit

/// Connected controllers (Xbox and other extended gamepads), each with a stable
/// id for as long as it stays connected, read as plain GamepadState values.
final class Controllers {
    private var connected: [Int: GCController] = [:]
    private var nextID = 0
    private var observers: [NSObjectProtocol] = []

    init() {
        let center = NotificationCenter.default
        observers.append(center.addObserver(forName: .GCControllerDidConnect, object: nil, queue: .main) { [weak self] note in
            if let controller = note.object as? GCController { self?.add(controller) }
        })
        observers.append(center.addObserver(forName: .GCControllerDidDisconnect, object: nil, queue: .main) { [weak self] note in
            if let controller = note.object as? GCController { self?.remove(controller) }
        })
        GCController.controllers().forEach(add)
        GCController.startWirelessControllerDiscovery {}
    }

    deinit {
        observers.forEach(NotificationCenter.default.removeObserver)
    }

    private func add(_ controller: GCController) {
        guard controller.extendedGamepad != nil, !connected.values.contains(where: { $0 === controller }) else { return }
        connected[nextID] = controller
        nextID += 1
    }

    private func remove(_ controller: GCController) {
        for (id, c) in connected where c === controller { connected[id] = nil }
    }

    /// The state of every connected controller, by id.
    func states() -> [Int: GamepadState] {
        var out: [Int: GamepadState] = [:]
        for (id, controller) in connected {
            guard let pad = controller.extendedGamepad else { continue }
            out[id] = GamepadState(
                leftX: pad.leftThumbstick.xAxis.value, leftY: pad.leftThumbstick.yAxis.value,
                dpadLeft: pad.dpad.left.isPressed, dpadRight: pad.dpad.right.isPressed,
                dpadUp: pad.dpad.up.isPressed, dpadDown: pad.dpad.down.isPressed,
                south: pad.buttonA.isPressed, east: pad.buttonB.isPressed, west: pad.buttonX.isPressed,
                north: pad.buttonY.isPressed, menu: pad.buttonMenu.isPressed)
        }
        return out
    }
}
