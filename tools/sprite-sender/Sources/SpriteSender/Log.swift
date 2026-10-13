import Foundation

/// stderr, one stamped line each. launchd keeps the file (dotfiles'
/// LaunchAgent). No line names a GIF, a path into the collection or the
/// sprite name: the log lives outside every repository, but a line is what
/// gets pasted.
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
