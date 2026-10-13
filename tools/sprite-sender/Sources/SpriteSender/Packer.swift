import Foundation
import SpriteSenderCore

/// Runs tools/sprite_pack.py (standard library only, so macOS's own
/// python3) on the chosen GIFs. The script comes from the app bundle's
/// Resources (package.sh copies it and gifpics.py there) or, for a bare
/// build, from `tools/` above this source file.
enum Packer {
    static var script: URL {
        if let resources = Bundle.main.resourceURL {
            let bundled = resources.appendingPathComponent("sprite_pack.py")
            if FileManager.default.isReadableFile(atPath: bundled.path) {
                return bundled
            }
        }
        // Sources/SpriteSender/Packer.swift -> tools/sprite_pack.py
        return URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
            .deletingLastPathComponent().appendingPathComponent("sprite_pack.py")
    }

    enum Error: Swift.Error {
        case converter(status: Int32, stderr: String)
        case cannotRead
    }

    /// The pack of the front GIF and the back one when given, named `name`.
    static func pack(front: URL, back: URL?, name: String) throws -> Data {
        let dir = FileManager.default.temporaryDirectory
            .appendingPathComponent("sprite-sender-\(getpid())-\(UInt32.random(in: 0...UInt32.max))")
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: dir) }
        let out = dir.appendingPathComponent("sprite.spk")

        let p = Process()
        p.executableURL = URL(fileURLWithPath: "/usr/bin/python3")
        p.arguments = [script.path, "pack", "--name", name, "-o", out.path, front.path] + (back.map { [$0.path] } ?? [])
        p.standardOutput = FileHandle.nullDevice
        let errPipe = Pipe()
        p.standardError = errPipe
        try p.run()
        let err = errPipe.fileHandleForReading.readDataToEndOfFile()
        p.waitUntilExit()
        guard p.terminationStatus == 0 else {
            throw Error.converter(status: p.terminationStatus,
                                  stderr: String(decoding: err, as: UTF8.self).trimmingCharacters(in: .whitespacesAndNewlines))
        }
        guard let data = FileManager.default.contents(atPath: out.path) else { throw Error.cannotRead }
        return data
    }
}
