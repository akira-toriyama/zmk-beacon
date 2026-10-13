import Foundation
import SpriteSenderCore

/// sprite-sender: the Mac side of the Prospector Dongle's daily sprite swap.
///
///   sprite-sender daily --config <sender.json> [--dry-run]
///       The next GIF of the rotation (SpriteSenderCore.Rotation), packed
///       with its back view when the config has one and sent to the device.
///       --dry-run packs and prints the sizes but moves no state and sends
///       nothing.
///   sprite-sender send <pack.spk>
///       One pack as it is: the hardware check.
///   sprite-sender report
///       Connect, print the device's report, leave.
///
/// Exit codes: Exit.swift. Nothing it prints names a GIF or the sprite.
@main
enum Main {
    static let version = "0.1.0"
    static let usage = """
        usage: sprite-sender daily --config <sender.json> [--dry-run]
               sprite-sender send <pack.spk>
               sprite-sender report
               sprite-sender --help | --version
        """

    static func main() {
        var args = Array(CommandLine.arguments.dropFirst())
        if args.contains("--help") || args.contains("-h") {
            print(usage)
            Exit.ok()
        }
        if args.contains("--version") || args.contains("-V") {
            print("sprite-sender \(version)")
            Exit.ok()
        }
        guard let verb = args.first else { Exit.usage(usage) }
        args.removeFirst()
        switch verb {
        case "daily":
            var config: String?
            var dryRun = false
            var i = 0
            while i < args.count {
                switch args[i] {
                case "--config":
                    guard i + 1 < args.count else { Exit.usage("--config needs a path") }
                    config = args[i + 1]
                    i += 2
                case "--dry-run":
                    dryRun = true
                    i += 1
                default:
                    Exit.usage("unknown argument '\(args[i])'\n\(usage)")
                }
            }
            guard let config else { Exit.usage("daily needs --config <sender.json>") }
            daily(configPath: config, dryRun: dryRun)
        case "send":
            guard args.count == 1 else { Exit.usage("send takes one pack\n\(usage)") }
            guard let pack = FileManager.default.contents(atPath: args[0]), !pack.isEmpty else {
                Exit.usage("cannot read the pack")
            }
            Sender(payload: pack).run()
        case "report":
            guard args.isEmpty else { Exit.usage("report takes no argument\n\(usage)") }
            Sender(payload: nil).run()
        default:
            Exit.usage("unknown command '\(verb)'\n\(usage)")
        }
    }

    static func daily(configPath: String, dryRun: Bool) -> Never {
        let configURL = URL(fileURLWithPath: configPath).standardizedFileURL
        guard let json = FileManager.default.contents(atPath: configURL.path) else {
            // The collection lives on an external drive: not mounted is a day skipped, not a config error.
            Exit.failure("cannot read the config file (the drive may not be mounted)")
        }
        let config: SenderConfig
        do {
            config = try SenderConfig(json: json, base: configURL.deletingLastPathComponent())
        } catch {
            Exit.usage("bad config file: \(error)")
        }
        let fm = FileManager.default
        let entries: [String]
        do {
            entries = try fm.contentsOfDirectory(atPath: config.frontDirectory.path)
        } catch {
            Exit.failure("cannot list the front directory")
        }
        let candidates = Rotation.candidates(entries)
        let last = fm.contents(atPath: config.stateFile.path).flatMap(Rotation.name(inState:))
        guard let chosen = Rotation.next(after: last, in: candidates) else {
            Exit.failure("the front directory has no GIF")
        }
        let index = candidates.firstIndex(of: chosen)! + 1
        Log.line("rotation: \(index) of \(candidates.count)\(last == nil ? ", no state" : "")")

        let name: String
        do {
            name = try SpriteName.from(fileName: chosen)
        } catch {
            if !dryRun { moveState(config, to: chosen) }
            Exit.failure("the GIF's file name gives no sprite name: \(error)")
        }
        let front = config.frontDirectory.appendingPathComponent(chosen)
        var back = config.backDirectory?.appendingPathComponent(chosen)
        if let b = back, !fm.isReadableFile(atPath: b.path) {
            Log.line("no back view for this GIF: front only")
            back = nil
        }

        if !dryRun { moveState(config, to: chosen) }
        let pack: Data
        do {
            pack = try Packer.pack(front: front, back: back, name: name)
        } catch {
            Exit.failure("the converter refused the GIF: \(error)")
        }
        Log.line("pack: \(pack.count) bytes, \(back == nil ? 1 : 2) animations, name of \(name.count) characters")
        if dryRun {
            Exit.ok()
        }
        Sender(payload: pack).run()
    }

    /// The rotation moves before the transfer (Rotation).
    static func moveState(_ config: SenderConfig, to chosen: String) {
        do {
            try Rotation.state(for: chosen).write(to: config.stateFile, options: .atomic)
        } catch {
            Exit.failure("cannot write the state file: \(error)")
        }
    }
}
