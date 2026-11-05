import Foundation

/// The config file: one JSON object. Absolute paths stay as they are and
/// relative ones resolve against the file's directory, so the file need not
/// sit with the collection.
///
///   { "front": "<dir>", "back": "<dir>", "state": "<file>", "range": "1 ~ 151",
///     "names": "<pokemon_species_names.csv>" }
///
/// `back` is optional: without it every pack holds the front animation only.
/// `state` is the file the rotation keeps its position in (Rotation); its
/// number names the GIF, so it stays out of anything public, and its directory
/// is made when missing.
/// `range` is optional: the numbers the rotation keeps (Rotation.NumberRange).
/// Without it, every `<number>.gif` of the front directory.
/// `names` is optional: the CSV tools/name_dictionary.py takes a GIF's sprite
/// name from. Without it the name is the number (SpriteName.choose).
/// `order` is a config error: the order file went with the numbered collection,
/// and a config that still names one was written for a sender that read it.
public struct SenderConfig: Equatable, Sendable {
    public var frontDirectory: URL
    public var backDirectory: URL?
    public var stateFile: URL
    public var range: Rotation.NumberRange?
    public var namesFile: URL?

    public init(
        frontDirectory: URL, backDirectory: URL?, stateFile: URL,
        range: Rotation.NumberRange? = nil, namesFile: URL? = nil
    ) {
        self.frontDirectory = frontDirectory
        self.backDirectory = backDirectory
        self.stateFile = stateFile
        self.range = range
        self.namesFile = namesFile
    }

    private struct File: Decodable {
        var front: String
        var back: String?
        var state: String
        /// Only to refuse it: JSONDecoder ignores a key that is not declared.
        var order: String?
        var range: String?
        var names: String?
    }

    public enum Error: Swift.Error, Equatable {
        case notAnObject(String)
        case emptyPath(String)
        /// Without the text: this error reaches the log.
        case badRange
        case orderIsGone
    }

    /// Parses the file's bytes; `base` is the directory the relative paths
    /// resolve against (the config file's).
    public init(json: Data, base: URL) throws {
        let file: File
        do {
            file = try JSONDecoder().decode(File.self, from: json)
        } catch {
            throw Error.notAnObject(String(describing: error))
        }
        guard file.order == nil else { throw Error.orderIsGone }
        func resolve(_ path: String, _ key: String) throws -> URL {
            guard !path.isEmpty else { throw Error.emptyPath(key) }
            return URL(fileURLWithPath: path, relativeTo: base).standardizedFileURL
        }
        frontDirectory = try resolve(file.front, "front")
        backDirectory = try file.back.map { try resolve($0, "back") }
        stateFile = try resolve(file.state, "state")
        if let text = file.range {
            guard let numbers = Rotation.NumberRange(text) else { throw Error.badRange }
            range = numbers
        }
        namesFile = try file.names.map { try resolve($0, "names") }
    }
}
