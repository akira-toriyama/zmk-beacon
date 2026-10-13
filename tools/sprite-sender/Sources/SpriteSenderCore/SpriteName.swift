import Foundation

/// The sprite name from the GIF's file name, as canon's assets/sprite-name.sh
/// derives it for a build (the user's rule, projects t-er81): the extension
/// dropped (`${name%.*}`: `.gif` alone leaves nothing), whitespace trimmed at
/// both ends, the first letter upper-cased. The pack takes printable ASCII,
/// 15 characters at most (tools/sprite_pack.py NAME_LEN); a longer name is
/// cut, as the HP bar would cut it, and trimmed again, and any other
/// character is an error, so a wrong name never reaches the device.
public enum SpriteName {
    public static let maxLength = 15

    public enum Error: Swift.Error, Equatable {
        case empty
        case notPrintableASCII
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
        let name = (trimmed.prefix(1).uppercased() + trimmed.dropFirst()).prefix(maxLength)
        let cut = name.trimmingCharacters(in: .whitespaces)
        guard !cut.isEmpty else { throw Error.empty }
        return cut
    }
}
