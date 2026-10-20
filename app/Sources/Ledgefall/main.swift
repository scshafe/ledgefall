import AppKit
import LedgefallKit
import MetalKit
import SwiftUI

// A MTKView that takes keyboard focus, records which keys are held and reports key presses.
final class GameView: MTKView {
    weak var model: AppModel?

    override var acceptsFirstResponder: Bool { true }

    override func keyDown(with event: NSEvent) {
        guard let model else { return }
        if model.flow.phase == .lobby {  // the lobby address is being typed
            if event.keyCode == 51 {
                model.backspace()
            } else if event.modifierFlags.contains(.command) {
                if event.charactersIgnoringModifiers == "v", let text = NSPasteboard.general.string(forType: .string) { model.typed(text) }
            } else if let text = event.characters {
                model.typed(text)
            }
        }
        model.heldKeys.insert(event.keyCode)
        if !event.isARepeat { model.keyPressed(event.keyCode) }
    }

    override func keyUp(with event: NSEvent) { model?.heldKeys.remove(event.keyCode) }

    // Modifier keys (shift, control, option, command) send no key down or up, only a change of flags. The
    // flags' device-dependent bits say which side is held, so the two shift keys are told apart.
    private static let modifierBits: [UInt16: UInt] = [
        56: 0x2, 60: 0x4, 59: 0x1, 62: 0x2000, 58: 0x20, 61: 0x40, 55: 0x8, 54: 0x10,
    ]

    override func flagsChanged(with event: NSEvent) {
        guard let model, let bit = GameView.modifierBits[event.keyCode] else { return }
        if event.modifierFlags.rawValue & bit != 0 {
            if model.heldKeys.insert(event.keyCode).inserted { model.keyPressed(event.keyCode) }
        } else {
            model.heldKeys.remove(event.keyCode)
        }
    }

    // Keys held when the window loses focus would otherwise stay "held".
    override func resignFirstResponder() -> Bool {
        model?.heldKeys.removeAll()
        return super.resignFirstResponder()
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    private var window: NSWindow?
    private var renderer: Renderer?
    private let model = AppModel()

    func applicationDidFinishLaunching(_ notification: Notification) {
        let rect = NSRect(x: 0, y: 0, width: 1280, height: 720)
        let view = GameView(frame: rect, device: MTLCreateSystemDefaultDevice())
        view.model = model
        view.preferredFramesPerSecond = 60
        view.colorPixelFormat = .bgra8Unorm
        guard let renderer = Renderer(view: view, model: model) else { fatalError("Metal is not available") }
        view.delegate = renderer
        self.renderer = renderer

        // The SwiftUI screens sit over the Metal view; the Metal view keeps the keyboard.
        let overlay = NSHostingView(rootView: OverlayView(model: model))
        let content = NSView(frame: rect)
        for sub in [view, overlay] {
            sub.frame = content.bounds
            sub.autoresizingMask = [.width, .height]
            content.addSubview(sub)
        }

        let window = NSWindow(contentRect: rect, styleMask: [.titled, .closable, .miniaturizable, .resizable],
                              backing: .buffered, defer: false)
        window.title = "Ledgefall"
        window.contentView = content
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
