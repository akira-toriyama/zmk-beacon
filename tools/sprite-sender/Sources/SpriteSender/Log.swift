import Foundation

/// stderr, one stamped line each. launchd keeps the file (dotfiles'
/// LaunchAgent). No line names a GIF, a path into the collection, the
/// sprite name, its length or the GIF's place in the rotation: the log
/// lives outside every repository, but a line is what gets pasted, and a
/// place in a numbered order names the subject, as a rare length does
/// matched against the public list of names. Every line still tells
/// something of the GIF: the name's alphabet (the `name:` line, the
/// report's `name=`) says whether the name dictionary's name fit the HP
/// bar, which with a complete dictionary is a fact about the name; a run
/// that cannot name the GIF says that neither the name dictionary's name
/// nor the file name serves; the pack's size (the `pack:`,
/// `start` and `end answered:` lines, the erase time of `begin answered`,
/// the report's expected and received) can name the GIF to someone who
/// makes packs from the same collection. So no line goes anywhere public.
enum Log {
    private static let stamp: ISO8601DateFormatter = {
        let f = ISO8601DateFormatter()
        f.timeZone = .current
        f.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
        return f
    }()

    static func line(_ message: String) {
        FileHandle.standardError.write(Data("\(stamp.string(from: Date())) \(message)\n".utf8))
    }
}
