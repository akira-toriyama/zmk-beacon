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

    /// The GIF after `last` in the rotation, the first when `last` is unknown
    /// (no state yet, or a file renamed away), nil when there is no GIF.
    public static func next(after last: String?, in candidates: [String]) -> String? {
        guard let first = candidates.first else { return nil }
        guard let last, let i = candidates.firstIndex(of: last) else { return first }
        return candidates[(i + 1) % candidates.count]
    }

    /// The state file's content for a GIF, and the GIF a state file names.
    public static func state(for name: String) -> Data {
        Data((name + "\n").utf8)
    }

    public static func name(inState data: Data) -> String? {
        let text = String(decoding: data, as: UTF8.self)
        let line = text.split(separator: "\n", maxSplits: 1, omittingEmptySubsequences: false).first ?? ""
        let name = line.trimmingCharacters(in: .whitespaces)
        return name.isEmpty ? nil : name
    }
}
