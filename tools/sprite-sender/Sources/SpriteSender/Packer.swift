import Foundation
import SpriteSenderCore

/// Runs tools/sprite_pack.py (Tools) on the chosen GIFs.
enum Packer {
    enum Error: Swift.Error, CustomStringConvertible {
        case cannotRead

        var description: String { "the converter wrote no pack" }
    }

    /// The pack of the front GIF and the back one when given, named `name`,
    /// which goes to the converter on stdin (`--name -`): as an argument it
    /// would arrive decomposed (Tools.run).
    static func pack(front: URL, back: URL?, name: String) throws -> Data {
        let dir = FileManager.default.temporaryDirectory
            .appendingPathComponent("sprite-sender-\(getpid())-\(UInt32.random(in: 0...UInt32.max))")
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: dir) }
        let out = dir.appendingPathComponent("sprite.spk")

        _ = try Tools.run("sprite_pack.py", ["pack", "--name", "-", "-o", out.path, front.path] + (back.map { [$0.path] } ?? []),
                          input: Data(name.utf8))
        guard let data = FileManager.default.contents(atPath: out.path) else { throw Error.cannotRead }
        return data
    }
}
