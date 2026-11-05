import Foundation

/// The sprite name of a pack: 15 bytes of UTF-8 at most, all printable ASCII
/// or all katakana (tools/sprite_pack.py check_name: the two fonts of the HP
/// bar). Which name a GIF gets is tools/name_dictionary.py's rule, where the
/// Actions route can share it (the user's rule, canon t-rq9h: katakana when it
/// can be shown, else as before); here is only what to do with the tool's line.
public enum SpriteName {
    public static let maxBytes = 15

    /// The characters of the katakana font, by block: sprite_pack.py's
    /// KATAKANA_RANGES, which tools/pkmn_font.py renders in full (the font is
    /// public and a name names the subject, so it is never cut to a name).
    /// Change the two together.
    public static let katakanaRanges: [ClosedRange<UInt32>] = [
        0x2640...0x2640, 0x2642...0x2642, 0x30A1...0x30FC, 0xFF10...0xFF1A, 0xFF21...0xFF3A,
    ]

    /// Where a name came from, for the log: its alphabet, not its length nor
    /// whether an ASCII name is the dictionary's or the number (matched against
    /// the public list of names, a rare length narrows the subject down to a
    /// few). The alphabet still says whether the dictionary's katakana name fit
    /// (with a complete dictionary, ASCII is a name of six katakana or more),
    /// so the line stays out of anything public (Log.swift).
    public enum Source: Equatable, Sendable, CustomStringConvertible {
        case katakana
        case ascii

        public var description: String {
            switch self {
            case .katakana: return "katakana from the name dictionary"
            case .ascii: return "ASCII"
            }
        }
    }

    public struct Choice: Equatable, Sendable {
        public let name: String
        public let source: Source

        public init(name: String, source: Source) {
            self.name = name
            self.source = source
        }
    }

    /// The name of GIF `number`: the first line the name tool printed, as its
    /// bytes are (composed: String's == takes NFD for NFC), else the number, so
    /// that a GIF always has a name. `toolOutput` nil: the config names no CSV;
    /// an empty line: the CSV has no name of this number that the HP bar shows.
    public static func choose(number: Int, toolOutput: Data?) -> Choice {
        let line = toolOutput?.prefix { $0 != 0x0a } ?? Data()
        guard !line.isEmpty else {
            return Choice(name: String(String(number).prefix(maxBytes)), source: .ascii)
        }
        return Choice(name: String(decoding: line, as: UTF8.self), source: line.allSatisfy { $0 < 0x80 } ? .ascii : .katakana)
    }
}
