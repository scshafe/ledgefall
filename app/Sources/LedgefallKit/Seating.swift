/// A device that can drive a seat.
public enum InputDevice: Hashable {
    case keyboard(cluster: Int)  // an index into KeyLayout.clusters
    case controller(id: Int)
}

/// What every device is doing this tick.
public struct DeviceFrame {
    public var keys: Set<UInt16>
    public var pads: [Int: GamepadState]  // by controller id; a controller that is unplugged is absent

    public init(keys: Set<UInt16> = [], pads: [Int: GamepadState] = [:]) {
        self.keys = keys
        self.pads = pads
    }
}

/// Which device drives which seat. A seat without a device is for a bot; a
/// device joins by being used (a key of its cluster, a button or the stick)
/// and takes the lowest free seat, and leaves when its controller is unplugged.
public struct Seating {
    public static let seatCount = 4

    public private(set) var devices: [InputDevice?] = Array(repeating: nil, count: Seating.seatCount)

    public init() {}

    public struct Update: Equatable {
        public var inputs: [PlayerInput]  // one per seat; empty for seats without a device
        public var joined: [Int]          // seats that gained a device this tick
        public var left: [Int]            // seats that lost one
    }

    public func seat(of device: InputDevice) -> Int? {
        devices.firstIndex(of: device)
    }

    @discardableResult
    private mutating func join(_ device: InputDevice) -> Int? {
        guard seat(of: device) == nil, let free = devices.firstIndex(where: { $0 == nil }) else { return nil }
        devices[free] = device
        return free
    }

    public mutating func update(_ frame: DeviceFrame) -> Update {
        var joined: [Int] = [], left: [Int] = []

        for (seat, device) in devices.enumerated() {
            if case .controller(let id)? = device, frame.pads[id] == nil {
                devices[seat] = nil
                left.append(seat)
            }
        }
        for (index, layout) in KeyLayout.clusters.enumerated() where layout.isActive(held: frame.keys) {
            if let seat = join(.keyboard(cluster: index)) { joined.append(seat) }
        }
        for id in frame.pads.keys.sorted() where frame.pads[id]?.isActive == true {
            if let seat = join(.controller(id: id)) { joined.append(seat) }
        }

        let inputs = devices.map { device -> PlayerInput in
            switch device {
            case .keyboard(let cluster)?: return KeyLayout.clusters[cluster].input(held: frame.keys)
            case .controller(let id)?: return frame.pads[id].map(GamepadMapping.input) ?? PlayerInput()
            case nil: return PlayerInput()
            }
        }
        return Update(inputs: inputs, joined: joined, left: left)
    }
}
