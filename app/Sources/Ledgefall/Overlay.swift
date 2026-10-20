import LedgefallKit
import SwiftUI

private func seatColor(_ seat: Int) -> Color {
    let c = seatColors[seat % seatColors.count]
    return Color(red: Double(c.x), green: Double(c.y), blue: Double(c.z))
}

/// The screens drawn over the Metal view: the stock readout while a match is on,
/// and the setup, pause and results panels.
struct OverlayView: View {
    @ObservedObject var model: AppModel

    var body: some View {
        ZStack {
            if ![.setup, .browsing, .connecting, .replays, .controls, .lobby].contains(model.flow.phase) { stocksBar }
            switch model.flow.phase {
            case .setup: setupPanel
            case .browsing: browsingPanel
            case .connecting: connectingPanel
            case .replays: replaysPanel
            case .controls: controlsPanel
            case .lobby: lobbyPanel
            case .watching: watchingBar
            case .playing: EmptyView()
            case .paused: panel(title: "Paused", lines: ["Return / A: resume", "Esc / B: quit to setup"])
            case .results(let winner): resultsPanel(winner)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private var stocksBar: some View {
        VStack {
            HStack(alignment: .top, spacing: 24) {
                ForEach(0..<model.stocks.count, id: \.self) { seat in
                    VStack(alignment: .leading, spacing: 3) {
                        HStack(spacing: 6) {
                            Text("P\(seat + 1)\(model.localSeats.contains(seat) ? (model.localSeats.count > 1 ? " (here)" : " (you)") : model.humans[seat] ? "" : " (bot)")").bold()
                            Text(model.stocks[seat] > 0 ? String(repeating: "●", count: model.stocks[seat]) : "out")
                        }
                        if seat < model.kits.count {
                            let kit = model.kits[seat]
                            Text("\(kit.fighter.name)\(kit.team >= 0 ? " · team \(kit.team == 0 ? "A" : "B")" : "")")
                                .font(.caption)
                            HStack(spacing: 8) {
                                ForEach(0..<3, id: \.self) { slot in
                                    let left = kit.cooldown[slot]
                                    Text(left == 0 ? "●" : String(format: "%.1f", Double(left) / 60))
                                        .font(.caption.monospacedDigit())
                                        .opacity(left == 0 ? 1 : 0.55)
                                }
                            }
                        }
                    }
                    .foregroundColor(seatColor(seat))
                }
            }
            .padding(10)
            .background(Color.black.opacity(0.45))
            .cornerRadius(8)
            .padding(.top, 12)
            Spacer()
        }
    }

    private var setupPanel: some View {
        let flow = model.flow
        return VStack(alignment: .leading, spacing: 12) {
            Text("Ledgefall").font(.system(size: 40, weight: .bold))
            row("Mode", value: flow.mode.name, selected: flow.selectedRow == Flow.modeRow)
            row("On this Mac", value: flow.mode == .host || flow.mode == .join ? "\(flow.localPlayers) online" : "(online modes)", selected: flow.selectedRow == Flow.localRow)
            row("Players", value: "\(flow.players)", selected: flow.selectedRow == Flow.playersRow)
            row("Stocks", value: "\(flow.stocks)", selected: flow.selectedRow == Flow.stocksRow)
            row("Stage", value: flow.arena.name, selected: flow.selectedRow == Flow.stageRow)
            row("Teams", value: flow.teamMode.name, selected: flow.selectedRow == Flow.teamsRow)
            ForEach(0..<flow.players, id: \.self) { seat in
                row("Seat \(seat + 1)", value: flow.fighters[seat].name, selected: flow.selectedRow == Flow.firstSeatRow + seat)
                    .foregroundColor(seatColor(seat))
            }
            row(flow.mode == .join ? "Find a room" : flow.mode == .host ? "Open the room" : flow.mode == .controls ? "Change the keys" : flow.mode == .lobby ? "Set the lobby address" : "Start", value: nil, selected: flow.selectedRow == flow.startRow)
            if flow.mode != .local {
                Text("Lobby: \(model.lobbyAddress ?? "not set (Mode: Set the lobby address)")")
                    .font(.footnote).foregroundColor(.secondary)
            }
            Text("Arrows or d-pad to choose, Return or A to confirm.\nIn a match: WASD, arrows or IJKL (jump on the top key; Q E R, . / right shift, or U O P are the three moves; the Mode row can change them), or a controller (A jump, X, B, Y the moves), to take a seat from the bots. Esc or the menu button pauses.")
                .font(.footnote)
                .foregroundColor(.secondary)
        }
        .padding(28)
        .background(Color.black.opacity(0.7))
        .cornerRadius(12)
        .foregroundColor(.white)
    }

    private var connectingPanel: some View {
        let flow = model.flow
        return panel(title: flow.failed ? "Could not play online" : "Online",
                     lines: [flow.status, flow.failed ? "Return / A or Esc / B: back" : "Esc / B: leave"])
    }

    private var replaysPanel: some View {
        let flow = model.flow
        return VStack(alignment: .leading, spacing: 10) {
            Text("Saved replays").font(.system(size: 34, weight: .bold))
            if flow.replays.isEmpty {
                Text("None yet: every match you play is saved.").font(.title3)
            }
            ForEach(Array(flow.replays.prefix(12).enumerated()), id: \.element.file) { index, info in
                HStack {
                    Text(index == flow.replayIndex ? "▸" : " ")
                    Text(info.date.formatted(date: .abbreviated, time: .shortened)).foregroundColor(.secondary)
                    Text(info.title)
                }
                .font(.title3)
                .foregroundColor(index == flow.replayIndex ? .yellow : .white)
            }
            Text("Arrows or d-pad to choose, Return or A to watch, Esc or B to go back.")
                .font(.footnote).foregroundColor(.secondary)
        }
        .padding(28)
        .background(Color.black.opacity(0.7))
        .cornerRadius(12)
        .foregroundColor(.white)
    }

    private var controlsPanel: some View {
        let flow = model.flow
        let layout = flow.bindings.layouts[flow.controlsCluster]
        return VStack(alignment: .leading, spacing: 10) {
            Text("Keys").font(.system(size: 34, weight: .bold))
            Text("◂ Keys \(flow.controlsCluster + 1) (starts as \(KeyBindings.clusterNames[flow.controlsCluster])) ▸").font(.title2)
            ForEach(KeyAction.allCases, id: \.rawValue) { action in
                let selected = flow.controlsRow == action.rawValue
                HStack {
                    Text(selected ? "▸" : " ")
                    Text(action.name).frame(width: 90, alignment: .leading)
                    Text(selected && flow.capturing ? "press a key..." : KeyName.label(layout[action]))
                }
                .font(.title3)
                .foregroundColor(selected ? .yellow : .white)
            }
            HStack {
                Text(flow.controlsRow == Flow.deadZoneRow ? "▸" : " ")
                Text("Stick dead zone")
                Text("◂ \(flow.deadZonePercent)% ▸")
            }
            .font(.title3)
            .foregroundColor(flow.controlsRow == Flow.deadZoneRow ? .yellow : .white)
            HStack {
                Text(flow.controlsRow == Flow.resetRow ? "▸" : " ")
                Text("Back to the standard keys and dead zone")
            }
            .font(.title3)
            .foregroundColor(flow.controlsRow == Flow.resetRow ? .yellow : .white)
            Text("Left and right switch cluster (or change the dead zone: how far a stick must move before it counts, for drifting sticks), Return picks a key to change, then press the new one (Esc cancels). A key another action had goes to the one you took it from. Esc goes back.")
                .font(.footnote).foregroundColor(.secondary)
        }
        .padding(28)
        .background(Color.black.opacity(0.7))
        .cornerRadius(12)
        .foregroundColor(.white)
    }

    private var lobbyPanel: some View {
        let flow = model.flow
        return VStack(alignment: .leading, spacing: 10) {
            Text("Lobby address").font(.system(size: 34, weight: .bold))
            Text(flow.lobbyDraft + "▏").font(.system(.title2, design: .monospaced))
            if !flow.lobbyError.isEmpty { Text(flow.lobbyError).foregroundColor(.orange) }
            if model.lobbyFromEnvironment {
                Text("LEDGEFALL_LOBBY is set and wins over the saved address.").foregroundColor(.orange)
            }
            Text("Type or paste (Cmd-V) the address of the lobby: host, host:port or https://host. Return saves it, Delete erases, Esc leaves it as it was. An empty address clears it.")
                .font(.footnote).foregroundColor(.secondary)
        }
        .padding(28)
        .background(Color.black.opacity(0.7))
        .cornerRadius(12)
        .foregroundColor(.white)
    }

    private var watchingBar: some View {
        VStack {
            Spacer()
            HStack(spacing: 16) {
                Text("Replay").bold()
                if let w = model.watchTicks {
                    Text("\(w.played / 60 / 60):" + String(format: "%02d", w.played / 60 % 60) + " of \(w.total / 60 / 60):" + String(format: "%02d", w.total / 60 % 60))
                        .monospacedDigit()
                }
                Text(model.flow.watchPaused ? "paused (Return resumes)" : "Return pauses")
                Text("Esc leaves").foregroundColor(.secondary)
            }
            .padding(10)
            .background(Color.black.opacity(0.55))
            .cornerRadius(8)
            .padding(.bottom, 14)
        }
        .foregroundColor(.white)
    }

    private var browsingPanel: some View {
        let flow = model.flow
        return VStack(alignment: .leading, spacing: 10) {
            Text("Open rooms").font(.system(size: 34, weight: .bold))
            if flow.rooms.isEmpty {
                Text("None yet. Someone has to host one.").font(.title3)
            }
            ForEach(Array(flow.rooms.enumerated()), id: \.element.id) { index, room in
                let setup = room.setup
                HStack {
                    Text(index == flow.roomIndex ? "▸" : " ")
                    Text(setup.map { "\($0.arena.name), \($0.stocks) stocks, " + $0.fighters.map(\.name).joined(separator: " / ") } ?? room.name)
                    Text("\(room.taken) of \(room.seatCount)").foregroundColor(.secondary)
                }
                .font(.title3)
                .foregroundColor(index == flow.roomIndex ? .yellow : .white)
            }
            Text("Arrows or d-pad to choose, Return or A to sit down, Esc or B to go back.")
                .font(.footnote).foregroundColor(.secondary)
        }
        .padding(28)
        .background(Color.black.opacity(0.7))
        .cornerRadius(12)
        .foregroundColor(.white)
    }

    private func row(_ name: String, value: String?, selected: Bool) -> some View {
        HStack {
            Text(selected ? "▸" : " ")
            Text(name).frame(width: 90, alignment: .leading)
            if let value { Text("◂ \(value) ▸") }
        }
        .font(.title2)
        .foregroundColor(selected ? .yellow : .white)
    }

    private func resultsPanel(_ winner: Int?) -> some View {
        panel(title: winner.map { "P\($0 + 1) wins" } ?? "No winner",
              lines: [model.flow.online ? "Return / A: rematch with the same players" : "Return / A: rematch", "Esc / B: setup", "(the replay is saved in Application Support/Ledgefall/Replays)"])
    }

    private func panel(title: String, lines: [String]) -> some View {
        VStack(spacing: 10) {
            Text(title).font(.system(size: 34, weight: .bold))
            ForEach(lines, id: \.self) { Text($0).font(.title3) }
        }
        .padding(28)
        .background(Color.black.opacity(0.7))
        .cornerRadius(12)
        .foregroundColor(.white)
    }
}
