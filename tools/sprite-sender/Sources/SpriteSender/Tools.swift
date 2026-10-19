import Foundation

/// The Python tools of tools/ (standard library only, so macOS's own
/// python3), run as child processes: sprite_pack.py (Packer) and
/// sprite_colours.py (Colours). A script comes from the app bundle's
/// Resources (package.sh copies them there) or, for a bare build, from
/// `tools/` above this source file.
enum Tools {
    static func script(_ name: String) -> URL {
        if let resources = Bundle.main.resourceURL {
            let bundled = resources.appendingPathComponent(name)
            if FileManager.default.isReadableFile(atPath: bundled.path) {
                return bundled
            }
        }
        // Sources/SpriteSender/Tools.swift -> tools/<name>
        return URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
            .deletingLastPathComponent().appendingPathComponent(name)
    }

    enum Error: Swift.Error, CustomStringConvertible {
        /// The tool's exit status and its own `<tool>:` line when it printed
        /// one. A Python traceback (an unreadable file, say) names the GIF's
        /// path, so nothing else of its stderr is kept.
        case exited(tool: String, status: Int32, message: String?)

        var description: String {
            switch self {
            case .exited(let tool, let status, let message):
                return "\(tool) exited \(status)" + (message.map { ": \($0)" } ?? "")
            }
        }
    }

    /// Runs the tool with the arguments; its stdout, or Error.exited.
    static func run(_ name: String, _ arguments: [String]) throws -> Data {
        let p = Process()
        p.executableURL = URL(fileURLWithPath: "/usr/bin/python3")
        p.arguments = [script(name).path] + arguments
        let outPipe = Pipe()
        let errPipe = Pipe()
        p.standardOutput = outPipe
        p.standardError = errPipe
        try p.run()
        let out = outPipe.fileHandleForReading.readDataToEndOfFile()
        let err = errPipe.fileHandleForReading.readDataToEndOfFile()
        p.waitUntilExit()
        guard p.terminationStatus == 0 else {
            let prefix = name.hasSuffix(".py") ? String(name.dropLast(3)) + ":" : name + ":"
            let line = String(decoding: err, as: UTF8.self).split(separator: "\n").last { $0.hasPrefix(prefix) }
            throw Error.exited(tool: name, status: p.terminationStatus, message: line.map(String.init))
        }
        return out
    }
}
