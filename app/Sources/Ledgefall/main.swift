import AppKit
import LedgefallKit
import MetalKit

// A MTKView that takes keyboard focus and records which keys are held.
final class GameView: MTKView {
    private(set) var held = Set<UInt16>()

    override var acceptsFirstResponder: Bool { true }

    override func keyDown(with event: NSEvent) { held.insert(event.keyCode) }
    override func keyUp(with event: NSEvent) { held.remove(event.keyCode) }
    // Keys held when the window loses focus would otherwise stay "held".
    override func resignFirstResponder() -> Bool {
        held.removeAll()
        return super.resignFirstResponder()
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    private var window: NSWindow?
    private var renderer: Renderer?
    private var controllers: Controllers?

    func applicationDidFinishLaunching(_ notification: Notification) {
        // Every seat starts as a bot. A person joins by using a device (a keyboard
        // cluster: WASD, arrows or IJKL; or any button on a controller) and takes
        // the lowest free seat from the bots; an unplugged controller gives it back.
        guard let match = Match(players: 4, stocks: 3) else { fatalError("could not create a match") }
        for seat in 0..<4 { match.setBot(seat: seat, level: .normal) }
        var seating = Seating()
        let controllers = Controllers()
        let loop = GameLoop(match: match)

        let rect = NSRect(x: 0, y: 0, width: 1280, height: 720)
        let view = GameView(frame: rect, device: MTLCreateSystemDefaultDevice())
        view.preferredFramesPerSecond = 60
        view.colorPixelFormat = .bgra8Unorm
        guard let renderer = Renderer(view: view, loop: loop, inputs: { [unowned view] in
            let update = seating.update(DeviceFrame(keys: view.held, pads: controllers.states()))
            for seat in update.joined { match.setBot(seat: seat, level: nil) }
            for seat in update.left { match.setBot(seat: seat, level: .normal) }
            return update.inputs
        }) else { fatalError("Metal is not available") }
        view.delegate = renderer
        self.renderer = renderer
        self.controllers = controllers

        let window = NSWindow(contentRect: rect, styleMask: [.titled, .closable, .miniaturizable, .resizable],
                              backing: .buffered, defer: false)
        window.title = "Ledgefall"
        window.contentView = view
        window.center()
        window.makeKeyAndOrderFront(nil)
        window.makeFirstResponder(view)
        self.window = window
        NSApp.activate(ignoringOtherApps: true)
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}

let app = NSApplication.shared
app.setActivationPolicy(.regular)

// A minimal menu so Cmd-Q quits.
let mainMenu = NSMenu()
let appItem = NSMenuItem()
mainMenu.addItem(appItem)
let appMenu = NSMenu()
appMenu.addItem(withTitle: "Quit Ledgefall", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
appItem.submenu = appMenu
app.mainMenu = mainMenu

let delegate = AppDelegate()
app.delegate = delegate
app.run()
