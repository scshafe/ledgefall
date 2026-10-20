import Foundation

/// A saved replay, as the list shows it.
public struct ReplayInfo: Equatable {
    public var file: String
    public var date: Date
    public var setup: OnlineSetup
    public var ticks: Int

    public var seconds: Int { ticks / Match.ticksPerSecond }

    /// "Crumble Yard, 3 stocks, Mason / Glim, 1:42"
    public var title: String {
        let m = seconds / 60, s = seconds % 60
        return "\(setup.arena.name), \(setup.stocks) stocks, \(setup.fighters.map(\.name).joined(separator: " / ")), \(m):" + String(format: "%02d", s)
    }
}

/// The folder of saved replays (`~/Library/Application Support/Ledgefall/Replays` in the app).
public struct ReplayLibrary {
    public static let fileExtension = "lfreplay"
    public let folder: URL

    public init(folder: URL) { self.folder = folder }

    public static var standard: ReplayLibrary? {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first
            .map { ReplayLibrary(folder: $0.appendingPathComponent("Ledgefall/Replays", isDirectory: true)) }
    }

    @discardableResult
    public func save(_ data: Data, at date: Date = Date()) throws -> URL {
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        var stamp = Int(date.timeIntervalSince1970)
        var url = folder.appendingPathComponent("\(stamp).\(ReplayLibrary.fileExtension)")
        while FileManager.default.fileExists(atPath: url.path) {  // two matches in one second
            stamp += 1
            url = folder.appendingPathComponent("\(stamp).\(ReplayLibrary.fileExtension)")
        }
        try data.write(to: url)
        return url
    }

    /// The readable replays, newest first. Files that are not replays are skipped.
    public func list() -> [ReplayInfo] {
        let names = (try? FileManager.default.contentsOfDirectory(atPath: folder.path)) ?? []
        var infos: [ReplayInfo] = []
        for name in names where name.hasSuffix(".\(ReplayLibrary.fileExtension)") {
            let url = folder.appendingPathComponent(name)
            guard let data = try? Data(contentsOf: url), let replay = Replay(data: data) else { continue }
            let date = (try? url.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate) ?? Date(timeIntervalSince1970: 0)
            infos.append(ReplayInfo(file: name, date: date, setup: replay.setup, ticks: replay.ticks))
        }
        return infos.sorted { $0.date > $1.date || ($0.date == $1.date && $0.file > $1.file) }
    }

    public func load(_ info: ReplayInfo) -> Replay? {
        (try? Data(contentsOf: folder.appendingPathComponent(info.file))).flatMap(Replay.init(data:))
    }
}

/// Watching a replay: its match, stepped with the recorded inputs at the match's own speed.
public final class ReplayWatch {
    public let replay: Replay
    public let loop: GameLoop
    public var paused = false

    /// Nil if the replay's setup cannot be made into a match.
    public init?(_ replay: Replay) {
        guard let match = replay.makeMatch() else { return nil }
        self.replay = replay
        loop = GameLoop(match: match)
    }

    public var match: Match { loop.match }
    public var finished: Bool { loop.match.tick >= replay.ticks }
    /// 0...1
    public var progress: Double { replay.ticks == 0 ? 1 : min(Double(loop.match.tick) / Double(replay.ticks), 1) }

    public func advance(to now: Double) {
        if paused || finished {
            loop.idle(to: now)
            return
        }
        loop.advance(to: now) { [replay, loop] in replay.inputs(at: loop.match.tick) }
    }
}
