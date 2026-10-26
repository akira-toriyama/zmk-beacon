import Foundation
import SpriteSenderCore

/// sprite-sender: the Mac side of the Prospector Dongle's daily sprite swap.
///
///   sprite-sender daily --config <sender.json> [--dry-run]
///       The next GIF of the rotation (SpriteSenderCore.Rotation), packed
///       with its back view when the config has one, named after the name
///       dictionary when the config has one and the HP bar can show that name,
///       else after its file (SpriteSenderCore.SpriteName), and sent to the
///       device. --dry-run packs and prints the sizes but moves no state and
///       sends nothing.
///   sprite-sender send <pack.spk>
///       One pack as it is: the hardware check.
///   sprite-sender report
///       Connect, print the device's report, leave.
///   sprite-sender colours --config <sender.json> [--count N]
///       The colours the sprite on the device shows most, from the GIF the
///       state names (the one tried last) and its back view when the
///       collection has one: one line on stdout,
///       RRGGBB each, the one to light the keys with first
///       (tools/sprite_colours.py says how they are picked). canon's
///       dongle.py palette takes the line as slots 0.. (its t-0967).
///       Nothing is sent.
///
/// Exit codes: Exit.swift. Nothing it prints names a GIF or the sprite.
@main
enum Main {
    static let version = "0.1.0"
    static let usage = """
        usage: sprite-sender daily --config <sender.json> [--dry-run]
               sprite-sender send <pack.spk>
               sprite-sender report
               sprite-sender colours --config <sender.json> [--count N]
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
        case "colours":
            var config: String?
            var count: Int?
            var i = 0
            while i < args.count {
                switch args[i] {
                case "--config":
                    guard i + 1 < args.count else { Exit.usage("--config needs a path") }
                    config = args[i + 1]
                    i += 2
                case "--count":
                    guard i + 1 < args.count, let n = Int(args[i + 1]), n >= 1 else {
                        Exit.usage("--count takes a whole number from 1")
                    }
                    count = n
                    i += 2
                default:
                    Exit.usage("unknown argument '\(args[i])'\n\(usage)")
                }
            }
            guard let config else { Exit.usage("colours needs --config <sender.json>") }
            colours(configPath: config, count: count)
        default:
            Exit.usage("unknown command '\(verb)'\n\(usage)")
        }
    }

    static func config(at configPath: String) -> SenderConfig {
        let configURL = URL(fileURLWithPath: configPath).standardizedFileURL
        guard let json = FileManager.default.contents(atPath: configURL.path) else {
            // The collection lives on an external drive: not mounted is a day skipped, not a config error.
            Exit.failure("cannot read the config file (the drive may not be mounted)")
        }
        do {
            return try SenderConfig(json: json, base: configURL.deletingLastPathComponent())
        } catch SenderConfig.Error.badRange {
            Exit.usage("bad config file: range is not \"<first> ~ <last>\" or \"<first> ~\" (ASCII digits from 1, the ASCII tilde)")
        } catch {
            Exit.usage("bad config file: \(error)")
        }
    }

    static func daily(configPath: String, dryRun: Bool) -> Never {
        let config = config(at: configPath)
        let fm = FileManager.default
        let entries: [String]
        do {
            entries = try fm.contentsOfDirectory(atPath: config.frontDirectory.path)
        } catch {
            Exit.failure("cannot list the front directory")
        }
        var order: [String] = []
        if let orderFile = config.orderFile {
            guard let data = fm.contents(atPath: orderFile.path) else {
                Exit.usage("cannot read the order file the config names")
            }
            order = Rotation.order(in: data)
        }
        if let range = config.range, range.first > order.count {
            Exit.usage("the range starts after the order file's \(order.count) names")
        }
        var dictionary: NameDictionary?
        if let namesFile = config.namesFile {
            guard let data = fm.contents(atPath: namesFile.path) else {
                Exit.usage("cannot read the name dictionary the config names")
            }
            do {
                dictionary = try NameDictionary(json: data)
            } catch NameDictionary.Error.duplicateNumber {
                Exit.usage("bad name dictionary: a number appears twice")
            } catch {
                Exit.usage("bad name dictionary: not a JSON array of objects with an integer \"no\"")
            }
        }
        let candidates = Rotation.candidates(entries, order: order, range: config.range)
        let last = fm.contents(atPath: config.stateFile.path).flatMap(Rotation.name(inState:))
        guard let chosen = Rotation.next(after: last, in: candidates, order: order) else {
            Exit.failure(config.range == nil ? "the front directory has no GIF" : "the front directory has no GIF in the range")
        }
        let index = candidates.firstIndex(of: chosen)! + 1
        var about: [String] = []
        if !order.isEmpty { about.append("\(order.count) listed") }
        if let range = config.range { about.append("range \(range)") }
        if let dictionary { about.append("\(dictionary.count) names") }
        Log.line("rotation: \(index) of \(candidates.count)\(about.isEmpty ? "" : " (\(about.joined(separator: ", ")))")\(last == nil ? ", no state" : "")")

        let place = Rotation.place(of: chosen, in: order)
        let name: String
        do {
            let choice = try SpriteName.choose(fileName: chosen, place: place, dictionary: dictionary)
            name = choice.name
            Log.line("name: \(choice.source), \(name.count) characters, \(name.utf8.count) bytes")
        } catch {
            if !dryRun { moveState(config, to: chosen) }
            Exit.failure("the GIF's file name gives no sprite name: \(error)")
        }
        let front = config.frontDirectory.appendingPathComponent(chosen)
        let back = backView(config, of: chosen)

        if !dryRun {
            Sender.checkAuthorization()
            moveState(config, to: chosen)
        }
        guard isRegularFile(front) else {
            Exit.failure("the chosen entry is not a readable file")
        }
        let pack: Data
        do {
            pack = try Packer.pack(front: front, back: back, name: name)
        } catch {
            Exit.failure("the converter refused the GIF: \(error)")
        }
        guard !pack.isEmpty else { Exit.failure("the converter wrote an empty pack") }
        Log.line("pack: \(pack.count) bytes, \(back == nil ? 1 : 2) animations")
        if dryRun {
            Exit.ok()
        }
        Sender(payload: pack).run()
    }

    /// The GIF the state names is the sprite on the device, unless its
    /// transfer failed today (the state moves before the transfer, Rotation):
    /// then yesterday's sprite plays and the caller, which runs this after a
    /// daily that succeeded, never asks.
    static func colours(configPath: String, count: Int?) -> Never {
        let config = config(at: configPath)
        guard let last = FileManager.default.contents(atPath: config.stateFile.path).flatMap(Rotation.name(inState:)) else {
            Exit.failure("no state: nothing has been sent yet")
        }
        let front = config.frontDirectory.appendingPathComponent(last)
        guard isRegularFile(front) else {
            Exit.failure("the GIF the state names is not a readable file")
        }
        do {
            print(try Colours.line(front: front, back: backView(config, of: last), count: count))
        } catch {
            Exit.failure("no colours: \(error)")
        }
        Exit.ok()
    }

    /// The GIF's back view: the file of the same name in the config's back
    /// directory, when the collection has one. The one rule for the pack
    /// (daily) and the colours.
    static func backView(_ config: SenderConfig, of name: String) -> URL? {
        guard let back = config.backDirectory?.appendingPathComponent(name) else { return nil }
        guard isRegularFile(back) else {
            Log.line("no back view for this GIF: front only")
            return nil
        }
        return back
    }

    /// The rotation moves before the transfer (Rotation). The error is not
    /// printed: Foundation's names the path.
    static func moveState(_ config: SenderConfig, to chosen: String) {
        do {
            try Rotation.state(for: chosen).write(to: config.stateFile, options: .atomic)
        } catch {
            Exit.failure("cannot write the state file")
        }
    }

    static func isRegularFile(_ url: URL) -> Bool {
        (try? url.resourceValues(forKeys: [.isRegularFileKey]).isRegularFile) == true
            && FileManager.default.isReadableFile(atPath: url.path)
    }
}
