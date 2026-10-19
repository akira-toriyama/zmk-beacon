import Foundation

/// Runs tools/sprite_colours.py (Tools) on a GIF: the colours it shows most,
/// the one for the key LEDs' slot 0 first, as the tool prints them.
enum Colours {
    enum Error: Swift.Error, CustomStringConvertible {
        case empty

        var description: String { "the colour tool printed nothing" }
    }

    /// One line, `RRGGBB` per colour separated by spaces; `count` at most
    /// (the tool's default when nil).
    static func line(front: URL, count: Int?) throws -> String {
        let out = try Tools.run("sprite_colours.py", (count.map { ["--count", String($0)] } ?? []) + [front.path])
        let line = String(decoding: out, as: UTF8.self).trimmingCharacters(in: .whitespacesAndNewlines)
        guard !line.isEmpty else { throw Error.empty }
        return line
    }
}
