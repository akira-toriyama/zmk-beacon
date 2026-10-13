import Foundation

/// The daily rotation: the front directory's GIFs in file-name order, one a
/// day, around and around. The state is the name of the GIF tried last, and
/// it moves BEFORE the transfer: a GIF the device refuses costs one day, not
/// the rest of the rotation (canon t-mxb7, 2026-10-13).
public enum Rotation {
    /// The GIFs among a directory's entries, in byte order of their names.
    /// macOS keeps a file's attributes on an exFAT drive in a `._<name>`
    /// sidecar, which is no GIF.
    public static func candidates(_ entries: [String]) -> [String] {
        entries
            .filter { $0.lowercased().hasSuffix(".gif") && !$0.hasPrefix("._") }
            .sorted { Array($0.utf8).lexicographicallyPrecedes(Array($1.utf8)) }
    }

    /// The GIF after `last` in the rotation: the first without a state, the
    /// first that sorts after `last` when `last` is gone (renamed or deleted:
    /// the rotation goes on instead of starting over), the first again after
    /// the last one, nil when there is no GIF.
    public static func next(after last: String?, in candidates: [String]) -> String? {
        guard let first = candidates.first else { return nil }
        guard let last else { return first }
        if let i = candidates.firstIndex(of: last) {
            return candidates[(i + 1) % candidates.count]
        }
        let lastBytes = Array(last.utf8)
        return candidates.first { lastBytes.lexicographicallyPrecedes(Array($0.utf8)) } ?? first
    }

    /// The state file's content for a GIF, and the GIF a state file names:
    /// its first line, exactly (a file name may begin or end with a space;
    /// trimming it would turn the name into a stranger and the rotation
    /// would stay on that GIF for good).
    public static func state(for name: String) -> Data {
        Data((name + "\n").utf8)
    }

    public static func name(inState data: Data) -> String? {
        // On bytes: Swift's String takes "\r\n" for one character.
        var line = data.prefix { $0 != 0x0a }
        if line.last == 0x0d { line.removeLast() }
        return line.isEmpty ? nil : String(decoding: line, as: UTF8.self)
    }
}
