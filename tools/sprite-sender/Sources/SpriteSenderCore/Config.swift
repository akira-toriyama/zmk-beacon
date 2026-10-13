import Foundation

/// The config file: one JSON object next to the GIFs, off every repository
/// (its paths name the collection). Relative paths resolve against the file's
/// directory, so the whole collection moves as one.
///
///   { "front": "front", "back": "back", "state": "sender-state" }
///
/// `back` is optional: without it every pack holds the front animation only.
/// `state` is the file the rotation keeps its position in (Rotation).
public struct SenderConfig: Equatable, Sendable {
    public var frontDirectory: URL
    public var backDirectory: URL?
    public var stateFile: URL

    public init(frontDirectory: URL, backDirectory: URL?, stateFile: URL) {
        self.frontDirectory = frontDirectory
        self.backDirectory = backDirectory
        self.stateFile = stateFile
    }

    private struct File: Decodable {
        var front: String
        var back: String?
        var state: String
    }

    public enum Error: Swift.Error, Equatable {
        case notAnObject(String)
        case emptyPath(String)
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
    }
}
