import Foundation

/// The daily rotation: the front directory's GIFs in the order file's order,
/// then the rest in file-name order, one a day, around and around. The
/// state is the name of the GIF tried last, and it moves BEFORE the
/// transfer: a GIF the device refuses costs one day, not the rest of the
/// rotation (canon t-mxb7, 2026-10-13).
public enum Rotation {
    /// The GIFs among a directory's entries: those the order lists, in its
    /// order, then the others in byte order of their names (all of them
    /// without an order; canon t-9aga, 2026-10-13). A listed name that is
    /// no entry is skipped, a name listed twice counts once. macOS keeps a
    /// file's attributes on an exFAT drive in a `._<name>` sidecar, which
    /// is no GIF.
    public static func candidates(_ entries: [String], order: [String] = []) -> [String] {
        let gifs = Set(entries.filter { $0.lowercased().hasSuffix(".gif") && !$0.hasPrefix("._") })
        var listed: [String] = []
        var seen = Set<String>()
        for name in order where gifs.contains(name) && seen.insert(name).inserted {
            listed.append(name)
        }
        let rest = gifs.subtracting(seen)
            .sorted { Array($0.utf8).lexicographicallyPrecedes(Array($1.utf8)) }
        return listed + rest
    }

    /// The GIF after `last` in the rotation: the first without a state; when
    /// `last` is gone (renamed or deleted: the rotation goes on instead of
    /// starting over), the next listed after it that exists, or the first
    /// unlisted that sorts after it; the first again after the last one;
    /// nil when there is no GIF.
    public static func next(after last: String?, in candidates: [String], order: [String] = [])
        -> String?
    {
        guard let first = candidates.first else { return nil }
        guard let last else { return first }
        if let i = candidates.firstIndex(of: last) {
            return candidates[(i + 1) % candidates.count]
        }
        if let i = order.firstIndex(of: last) {
            return order[(i + 1)...].first { candidates.contains($0) } ?? first
        }
        let lastBytes = Array(last.utf8)
        let listed = Set(order)
        return candidates.first {
            !listed.contains($0) && lastBytes.lexicographicallyPrecedes(Array($0.utf8))
        } ?? first
    }

    /// The order file's names: one a line, exactly (as the state), empty
    /// lines skipped.
    public static func order(in data: Data) -> [String] {
        data.split(separator: 0x0a, omittingEmptySubsequences: true).compactMap { line in
            var line = line
            if line.last == 0x0d { line.removeLast() }
            return line.isEmpty ? nil : String(decoding: line, as: UTF8.self)
        }
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
