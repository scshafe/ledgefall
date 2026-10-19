/// What the app reads off a controller (GameController.framework's extended
/// gamepad, which is what Xbox controllers present as), as plain values so the
/// mapping to the core's input can be tested without a controller.
public struct GamepadState: Equatable {
    public var leftX: Float = 0, leftY: Float = 0  // -1...1, y up
    public var dpadLeft = false, dpadRight = false, dpadDown = false
    public var south = false, east = false, west = false, north = false  // Xbox A, B, X, Y

    public init(leftX: Float = 0, leftY: Float = 0, dpadLeft: Bool = false, dpadRight: Bool = false,
                dpadDown: Bool = false, south: Bool = false, east: Bool = false, west: Bool = false,
                north: Bool = false) {
        self.leftX = leftX
        self.leftY = leftY
        self.dpadLeft = dpadLeft
        self.dpadRight = dpadRight
        self.dpadDown = dpadDown
        self.south = south
        self.east = east
        self.west = west
        self.north = north
    }

    /// Any press or a stick pushed well off centre: how a controller asks to join.
    public var isActive: Bool {
        south || east || west || north || dpadLeft || dpadRight || dpadDown || abs(leftX) > 0.5 || abs(leftY) > 0.5
    }
}

/// The app owns dead zones (docs/architecture.md, "Input"): the core sees only
/// the quantised axis.
public enum GamepadMapping {
    public static let deadZone: Float = 0.2
    /// How far down the stick must be pushed to count as "down".
    public static let downThreshold: Float = 0.65

    public static func input(_ pad: GamepadState) -> PlayerInput {
        var axis: Float
        if pad.dpadLeft != pad.dpadRight {
            axis = pad.dpadRight ? 1 : -1
        } else {
            // Outside the dead zone, rescaled so the stick still reaches 0 at the edge and 1 at full tilt.
            let magnitude = abs(pad.leftX)
            axis = magnitude <= deadZone ? 0 : min((magnitude - deadZone) / (1 - deadZone), 1)
            if pad.leftX < 0 { axis = -axis }
        }
        return PlayerInput(
            moveX: Int8((axis * 100).rounded()),
            jump: pad.south || pad.north,
            down: pad.dpadDown || pad.leftY < -downThreshold)
    }
}
