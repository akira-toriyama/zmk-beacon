import Foundation

/// The daily rotation: the front directory's GIFs in the order file's order,
/// then the rest in file-name order, or only those at a range of the order's
/// places; one a day, around and around. The state is the name of the GIF
/// tried last, and it moves BEFORE the transfer: a GIF the device refuses
/// costs one day, not the rest of the rotation (canon t-mxb7, 2026-10-13).
public enum Rotation {
    /// Places of the order, counted from 1 over its names (Rotation.order),
    /// written as the user writes them: "1 ~ 151", or "100 ~" for every place
    /// from 100 to the order's end. The user wanted no list such as "1, 3, 5"
    /// (canon t-m7nh, 2026-10-14).
    public struct PlaceRange: Equatable, Sendable, CustomStringConvertible {
        public let first: Int
        /// nil: through the order's last name.
        public let last: Int?

        public init?(first: Int, last: Int?) {
            guard first >= 1, last.map({ first <= $0 }) ?? true else { return nil }
            self.first = first
            self.last = last
        }

        /// "first ~ last" or "first ~": ASCII digits and the ASCII tilde,
        /// spaces around them free.
        public init?(_ text: String) {
            let parts = text.split(separator: "~", maxSplits: 1, omittingEmptySubsequences: false)
                .map { $0.trimmingCharacters(in: .whitespaces) }
            guard parts.count == 2, let first = Self.number(parts[0]) else { return nil }
            if parts[1].isEmpty {
                self.init(first: first, last: nil)
            } else {
                guard let last = Self.number(parts[1]) else { return nil }
                self.init(first: first, last: last)
            }
        }

        public func contains(_ place: Int) -> Bool {
            place >= first && last.map { place <= $0 } ?? true
        }

        public var description: String {
            last.map { "\(first) ~ \($0)" } ?? "\(first) ~"
        }

        private static func number(_ text: String) -> Int? {
            guard !text.isEmpty, text.allSatisfy({ $0.isASCII && $0.isNumber }) else { return nil }
            return Int(text)
        }
    }

    /// The GIFs among a directory's entries: those the order lists, in its
    /// order, then the others in byte order of their names (all of them
    /// without an order; canon t-9aga, 2026-10-13). With a range, only the
    /// listed ones at its places and none of the others (canon t-m7nh). A
    /// listed name that is no entry is skipped, a name listed twice holds
    /// its first place only. macOS keeps a file's attributes on an exFAT
    /// drive in a `._<name>` sidecar, which is no GIF.
    public static func candidates(
        _ entries: [String], order: [String] = [], range: PlaceRange? = nil
    ) -> [String] {
        let gifs = Set(entries.filter { $0.lowercased().hasSuffix(".gif") && !$0.hasPrefix("._") })
        var listed: [String] = []
        var seen = Set<String>()
        var place = 0
        for name in order where seen.insert(name).inserted {
            place += 1
            if gifs.contains(name), range?.contains(place) ?? true {
                listed.append(name)
            }
        }
        guard range == nil else { return listed }
        let rest = gifs.subtracting(seen)
            .sorted { Array($0.utf8).lexicographicallyPrecedes(Array($1.utf8)) }
        return listed + rest
    }

    /// The GIF after `last` in the rotation: the first without a state; when
    /// `last` is not in it (renamed, deleted or outside the range: the
    /// rotation goes on instead of starting over), the next listed after it
    /// that is, else the first unlisted; for an unlisted `last`, the first
    /// unlisted that sorts after it; the first again after the last one; nil
    /// when there is no GIF.
    public static func next(after last: String?, in candidates: [String], order: [String] = [])
        -> String?
    {
        guard let first = candidates.first else { return nil }
        guard let last else { return first }
        if let i = candidates.firstIndex(of: last) {
            return candidates[(i + 1) % candidates.count]
        }
        let listed = Set(order)
        if let i = order.firstIndex(of: last) {
            return order[(i + 1)...].first { candidates.contains($0) }
                ?? candidates.first { !listed.contains($0) } ?? first
        }
        let lastBytes = Array(last.utf8)
        return candidates.first {
            !listed.contains($0) && lastBytes.lexicographicallyPrecedes(Array($0.utf8))
        } ?? first
    }

    /// The order file's names: one a line, exactly (as the state), empty
    /// lines skipped, a name's first line its place.
    public static func order(in data: Data) -> [String] {
        var seen = Set<String>()
        return data.split(separator: 0x0a, omittingEmptySubsequences: true).compactMap { line in
            var line = line
            if line.last == 0x0d { line.removeLast() }
            guard !line.isEmpty else { return nil }
            let name = String(decoding: line, as: UTF8.self)
            return seen.insert(name).inserted ? name : nil
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
