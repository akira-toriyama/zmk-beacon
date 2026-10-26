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

    /// Runs the tool with the arguments, `input` on its stdin when given; its
    /// stdout, or Error.exited. Text that must arrive as it is goes through
    /// `input`, never an argument: Process hands the child its arguments in
    /// the file system's representation, decomposed (a katakana with a
    /// dakuten became two code points, 2026-10-26), and a path survives that
    /// (the file systems look names up without regard to it) where a sprite
    /// name does not.
    static func run(_ name: String, _ arguments: [String], input: Data? = nil) throws -> Data {
        let p = Process()
        p.executableURL = URL(fileURLWithPath: "/usr/bin/python3")
        p.arguments = [script(name).path] + arguments
        let outPipe = Pipe()
        let errPipe = Pipe()
        p.standardOutput = outPipe
        p.standardError = errPipe
        let inPipe = input.map { _ in Pipe() }
        if let inPipe { p.standardInput = inPipe }
        try p.run()
        if let inPipe, let input {
            // Written whole before the outputs are read: the input is a few
            // bytes, far below the pipe's buffer, so this never blocks.
            inPipe.fileHandleForWriting.write(input)
            try inPipe.fileHandleForWriting.close()
        }
        // Both pipes drained at once: a child that fills one while the parent
        // reads the other to its end would block for good.
        var err = Data()
        let drained = DispatchGroup()
        drained.enter()
        DispatchQueue.global().async {
            err = errPipe.fileHandleForReading.readDataToEndOfFile()
            drained.leave()
        }
        let out = outPipe.fileHandleForReading.readDataToEndOfFile()
        drained.wait()
        p.waitUntilExit()
        guard p.terminationStatus == 0 else {
            let prefix = name.hasSuffix(".py") ? String(name.dropLast(3)) + ":" : name + ":"
            let line = String(decoding: err, as: UTF8.self).split(separator: "\n").last { $0.hasPrefix(prefix) }
            throw Error.exited(tool: name, status: p.terminationStatus, message: line.map(String.init))
        }
        return out
    }
}
