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
            if model.flow.phase != .setup { stocksBar }
            switch model.flow.phase {
            case .setup: setupPanel
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
                            Text("P\(seat + 1)\(model.humans[seat] ? "" : " (bot)")").bold()
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
            row("Players", value: "\(flow.players)", selected: flow.selectedRow == Flow.playersRow)
            row("Stocks", value: "\(flow.stocks)", selected: flow.selectedRow == Flow.stocksRow)
            row("Stage", value: flow.arena.name, selected: flow.selectedRow == Flow.stageRow)
            row("Teams", value: flow.teamMode.name, selected: flow.selectedRow == Flow.teamsRow)
            ForEach(0..<flow.players, id: \.self) { seat in
                row("Seat \(seat + 1)", value: flow.fighters[seat].name, selected: flow.selectedRow == Flow.firstSeatRow + seat)
                    .foregroundColor(seatColor(seat))
            }
            row("Start", value: nil, selected: flow.selectedRow == flow.startRow)
            Text("Arrows or d-pad to choose, Return or A to confirm.\nIn a match: WASD, arrows or IJKL (jump on the top key; Q E R, . / right shift, or U O P are the three moves), or a controller (A jump, X, B, Y the moves), to take a seat from the bots. Esc or the menu button pauses.")
                .font(.footnote)
                .foregroundColor(.secondary)
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
              lines: ["Return / A: rematch", "Esc / B: setup", "(the replay is saved in Application Support/Ledgefall/Replays)"])
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
