import Foundation

/// The name dictionary: a JSON file next to the config (SenderConfig `names`)
/// that gives a place of the order a Japanese name, written for the sprite
/// name when the HP bar can show it (SpriteName.choose). One array, one
/// object a place:
///
///   [{"no": 1, "ja": "<katakana>", "en": "<name>"}, ...]
///
/// `no` is the place (Rotation.place; with the user's order, the species'
/// National Pokédex number), `ja` the katakana name, `en` the English one,
/// which the sender does not use (tools/name_dictionary.py writes the file
/// from PokeAPI's CSV and compares `en` with the order's file names). Other
/// keys are ignored, so a field can be added without a sender change; `ja`
/// may be missing. The file names the collection's subjects, so it stays off
/// every repository, like the order file, and no error carries a value of
/// it, not even a number.
public struct NameDictionary: Equatable, Sendable {
    public enum Error: Swift.Error, Equatable {
        /// Not a JSON array of objects with an integer `no`.
        case notAnArrayOfEntries
        /// A `no` twice.
        case duplicateNumber
    }

    private var japanese: [Int: String]

    public init(japanese: [Int: String]) {
        self.japanese = japanese
    }

    private struct Entry: Decodable {
        var no: Int
        var ja: String?
    }

    public init(json: Data) throws {
        guard let entries = try? JSONDecoder().decode([Entry].self, from: json) else {
            throw Error.notAnArrayOfEntries
        }
        japanese = [:]
        var seen = Set<Int>()
        for entry in entries {
            guard seen.insert(entry.no).inserted else { throw Error.duplicateNumber }
            if let ja = entry.ja {
                japanese[entry.no] = ja
            }
        }
    }

    /// The number of places with a Japanese name.
    public var count: Int { japanese.count }

    /// The Japanese name of a place, as the file has it (SpriteName.katakana
    /// normalizes and checks it).
    public func japaneseName(_ number: Int) -> String? {
        japanese[number]
    }
}
