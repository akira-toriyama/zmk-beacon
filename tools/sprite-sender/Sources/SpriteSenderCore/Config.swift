import Foundation

/// The config file: one JSON object next to the GIFs, off every repository
/// (its paths name the collection). Relative paths resolve against the file's
/// directory, so the whole collection moves as one.
///
///   { "front": "front", "back": "back", "state": "sender-state", "order": "order.txt",
///     "range": "1 ~ 151" }
///
/// `back` is optional: without it every pack holds the front animation only.
/// `state` is the file the rotation keeps its position in (Rotation).
/// `order` is optional: a file naming the GIFs in the order to show them,
/// one file name a line (Rotation; the user's collection lists its species
/// in National Pokédex order). Without it the rotation is file-name order.
/// `range` is optional and needs `order`: the places of the order the
/// rotation keeps (Rotation.PlaceRange; with the user's order, the species'
/// numbers). A sender older than the key ignores it and shows them all.
public struct SenderConfig: Equatable, Sendable {
    public var frontDirectory: URL
    public var backDirectory: URL?
    public var stateFile: URL
    public var orderFile: URL?
    public var range: Rotation.PlaceRange?

    public init(
        frontDirectory: URL, backDirectory: URL?, stateFile: URL, orderFile: URL? = nil,
        range: Rotation.PlaceRange? = nil
    ) {
        self.frontDirectory = frontDirectory
        self.backDirectory = backDirectory
        self.stateFile = stateFile
        self.orderFile = orderFile
        self.range = range
    }

    private struct File: Decodable {
        var front: String
        var back: String?
        var state: String
        var order: String?
        var range: String?
    }

    public enum Error: Swift.Error, Equatable {
        case notAnObject(String)
        case emptyPath(String)
        case badRange(String)
        case rangeWithoutOrder
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
        func resolve(_ path: String, _ key: String) throws -> URL {
            guard !path.isEmpty else { throw Error.emptyPath(key) }
            return URL(fileURLWithPath: path, relativeTo: base).standardizedFileURL
        }
        frontDirectory = try resolve(file.front, "front")
        backDirectory = try file.back.map { try resolve($0, "back") }
        stateFile = try resolve(file.state, "state")
        orderFile = try file.order.map { try resolve($0, "order") }
        if let text = file.range {
            guard let places = Rotation.PlaceRange(text) else { throw Error.badRange(text) }
            guard orderFile != nil else { throw Error.rangeWithoutOrder }
            range = places
        }
    }
}
