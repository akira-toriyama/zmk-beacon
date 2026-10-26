import Foundation

/// The sprite name of a pack: 15 bytes of UTF-8 at most, all printable ASCII
/// or all katakana (tools/sprite_pack.py check_name: the two fonts of the HP
/// bar). The sender prefers the katakana name of the name dictionary (the
/// user's rule, canon t-rq9h: katakana when it can be shown, else as before)
/// and falls back to the GIF's file name as canon's assets/sprite-name.py
/// derives it for a build (the user's rule, canon t-er81): the extension
/// dropped (`${name%.*}`: `.gif` alone leaves nothing), whitespace trimmed at
/// both ends, the first letter upper-cased, cut to 15 characters (bytes, in
/// ASCII), as the HP bar would cut it, and trimmed again; any other character
/// is an error, so a wrong name never reaches the device.
public enum SpriteName {
    public static let maxBytes = 15

    /// The characters of the katakana font, by block: sprite_pack.py's
    /// KATAKANA_RANGES, which tools/pkmn_font.py renders in full (the font is
    /// public and a name names the subject, so it is never cut to a name).
    /// Change the two together.
    public static let katakanaRanges: [ClosedRange<UInt32>] = [
        0x2640...0x2640, 0x2642...0x2642, 0x30A1...0x30FC, 0xFF10...0xFF1A, 0xFF21...0xFF3A,
    ]

    public enum Error: Swift.Error, Equatable {
        case empty
        case notPrintableASCII
        /// More than maxBytes of UTF-8.
        case tooLong
        /// A character the katakana font lacks (katakanaRanges).
        case outsideTheFont
    }

    public static func from(fileName: String) throws -> String {
        var stem = fileName
        if let dot = stem.lastIndex(of: ".") {
            stem = String(stem[..<dot])
        }
        let trimmed = stem.trimmingCharacters(in: .whitespaces)
        guard !trimmed.isEmpty else { throw Error.empty }
        guard trimmed.unicodeScalars.allSatisfy({ (0x20...0x7e).contains($0.value) }) else {
            throw Error.notPrintableASCII
        }
        let name = (trimmed.prefix(1).uppercased() + trimmed.dropFirst()).prefix(maxBytes)
        let cut = name.trimmingCharacters(in: .whitespaces)
        guard !cut.isEmpty else { throw Error.empty }
        return cut
    }

    /// A dictionary name as the pack takes it: composed (NFC: a dakuten as a
    /// combining mark would be a second character, which LVGL draws apart and
    /// the font lacks), at most maxBytes of UTF-8, every character in the
    /// katakana font. Never cut: a cut name would be another name.
    public static func katakana(_ text: String) throws -> String {
        let name = text.precomposedStringWithCanonicalMapping
        guard !name.isEmpty else { throw Error.empty }
        guard name.utf8.count <= maxBytes else { throw Error.tooLong }
        guard name.unicodeScalars.allSatisfy({ scalar in katakanaRanges.contains { $0.contains(scalar.value) } })
        else { throw Error.outsideTheFont }
        return name
    }

    /// Why the name came from the file name instead of the dictionary.
    public enum Fallback: Equatable, Sendable, CustomStringConvertible {
        case noDictionary
        case notInOrder
        case noEntry
        case tooLong
        case outsideTheFont

        public var description: String {
            switch self {
            case .noDictionary: return "the config names no dictionary"
            case .notInOrder: return "the GIF is not in the order"
            case .noEntry: return "the dictionary has no name for its place"
            case .tooLong: return "the dictionary's name is longer than \(maxBytes) bytes"
            case .outsideTheFont: return "the dictionary's name has a character the katakana font lacks"
            }
        }
    }

    /// Where a name came from, for the log: nothing of it names the subject.
    public enum Source: Equatable, Sendable, CustomStringConvertible {
        case katakana
        case fileName(Fallback)

        public var description: String {
            switch self {
            case .katakana: return "katakana from the name dictionary"
            case .fileName(let why): return "from the file name (\(why))"
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

    /// The name for a GIF: the dictionary's katakana name of its place in the
    /// order when there is one that the HP bar shows, else the file name's
    /// (from(fileName:), whose errors this throws).
    public static func choose(fileName: String, place: Int?, dictionary: NameDictionary?) throws -> Choice {
        let fallback: Fallback
        if let dictionary {
            if let place {
                if let text = dictionary.japaneseName(place) {
                    do {
                        return Choice(name: try katakana(text), source: .katakana)
                    } catch Error.tooLong {
                        fallback = .tooLong
                    } catch Error.outsideTheFont {
                        fallback = .outsideTheFont
                    } catch {
                        fallback = .noEntry
                    }
                } else {
                    fallback = .noEntry
                }
            } else {
                fallback = .notInOrder
            }
        } else {
            fallback = .noDictionary
        }
        return Choice(name: try from(fileName: fileName), source: .fileName(fallback))
    }
}
