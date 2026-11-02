import Foundation

/// stderr, one stamped line each. launchd keeps the file (dotfiles'
/// LaunchAgent). No line names a GIF, a path into the collection, the
/// sprite name or the GIF's place in the rotation (in a numbered order the
/// place names the subject): the log lives outside every repository, but a
/// line is what gets pasted. The name's length (the `name:` line, the
/// report's name_chars and name_bytes) still narrows the subject down: the
/// lengths of a few runs, or one line for a name of a rare length, can place
/// the GIF in the public order, so no such line goes anywhere public.
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
