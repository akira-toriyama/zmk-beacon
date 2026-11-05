import Foundation

/// The daily rotation: the front directory's `<number>.gif` files in number
/// order, or only those of a range of numbers; one a day, around and around.
/// The state is the number of the GIF tried last, and it moves BEFORE the
/// transfer: a GIF the device refuses costs one day, not the rest of the
/// rotation (canon t-mxb7, 2026-10-13).
public enum Rotation {
    /// Numbers of the collection, written as the user writes them: "1 ~ 151",
    /// or "100 ~" for every number from 100. The user wanted no list such as
    /// "1, 3, 5" (canon t-m7nh, 2026-10-14).
    public struct NumberRange: Equatable, Sendable, CustomStringConvertible {
        public let first: Int
        /// nil: every number from `first`.
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

        public func contains(_ number: Int) -> Bool {
            number >= first && last.map { number <= $0 } ?? true
        }

        public var description: String {
            last.map { "\(first) ~ \($0)" } ?? "\(first) ~"
        }

        private static func number(_ text: String) -> Int? {
            guard !text.isEmpty, text.allSatisfy({ $0.isASCII && $0.isNumber }) else { return nil }
            return Int(text)
        }
    }

    /// A positive integer written as `String(Int)` writes it, so that a number
    /// names one file: no sign, no leading zero, ASCII digits only.
    private static func number(_ text: String) -> Int? {
        guard let number = Int(text), number >= 1, String(number) == text else { return nil }
        return number
    }

    /// The number of a directory entry that belongs to the collection. Nothing
    /// else does: `<number>-<form>.gif`, a name that is no number, a `._<name>`
    /// sidecar, a subdirectory, `.GIF` (fileName(of:) gives `.gif`, which a
    /// case-sensitive volume would not find).
    public static func number(ofFileName name: String) -> Int? {
        guard name.hasSuffix(".gif") else { return nil }
        return number(String(name.dropLast(4)))
    }

    public static func fileName(of number: Int) -> String {
        "\(number).gif"
    }

    /// The rotation's numbers among a directory's entries, ascending.
    public static func candidates(_ entries: [String], range: NumberRange? = nil) -> [Int] {
        entries.compactMap(number(ofFileName:)).filter { range?.contains($0) ?? true }.sorted()
    }

    /// The number after `last`: the next one up, so a number that is gone or
    /// outside the range goes on from its place instead of starting over; the
    /// first again past the end, and without a state; nil when there is no GIF.
    public static func next(after last: Int?, in candidates: [Int]) -> Int? {
        guard let last else { return candidates.first }
        return candidates.first { $0 > last } ?? candidates.first
    }

    /// The state file's content for a GIF, and the GIF a state file names: its
    /// first line. Anything but a number there (a sender before the numbered
    /// collection kept a file name) is no state, and the rotation starts over.
    public static func state(for number: Int) -> Data {
        Data("\(number)\n".utf8)
    }

    public static func number(inState data: Data) -> Int? {
        // On bytes: Swift's String takes "\r\n" for one character.
        var line = data.prefix { $0 != 0x0a }
        if line.last == 0x0d { line.removeLast() }
        return number(String(decoding: line, as: UTF8.self))
    }
}
