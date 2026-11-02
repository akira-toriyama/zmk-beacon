import Foundation

/// Runs tools/sprite_colours.py (Tools) on a sprite's GIFs: the colours its
/// front and back show most, the one for the key LEDs' slot 0 first, as the
/// tool prints them.
enum Colours {
    enum Error: Swift.Error, CustomStringConvertible {
        case empty

        var description: String { "the colour tool printed nothing" }
    }

    /// One line, `RRGGBB` per colour separated by spaces: exactly `count`
    /// colours (the tool's default when nil), the tool cycling through the
    /// ones shown when fewer show. The back view, when the collection has
    /// one, goes after the front (the tool says how the two weigh).
    static func line(front: URL, back: URL?, count: Int?) throws -> String {
        let out = try Tools.run("sprite_colours.py",
                                (count.map { ["--count", String($0)] } ?? []) + [front.path] + (back.map { [$0.path] } ?? []))
        let line = String(decoding: out, as: UTF8.self).trimmingCharacters(in: .whitespacesAndNewlines)
        guard !line.isEmpty else { throw Error.empty }
        return line
    }
}
