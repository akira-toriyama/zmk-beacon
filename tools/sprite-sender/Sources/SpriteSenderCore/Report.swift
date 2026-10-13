import Foundation

/// `struct report` of zmk-beacon's src/ble_receiver.c, REPORT_VERSION 1: the
/// control characteristic's read. Little-endian, packed, 44 bytes.
public struct Report: Equatable, Sendable {
    public static let version: UInt8 = 1
    public static let size = 44

    public enum State: UInt8, Sendable { case idle = 0, receiving = 1 }
    public enum Result: UInt8, Sendable {
        case none = 0, shown = 1, short = 2, notAPack = 3, rejected = 4, flashError = 5, disconnected = 6
    }
    public enum Source: UInt8, Sendable { case none = 0, embedded = 1, room = 2 }

    public var version: UInt8
    public var state: UInt8
    public var result: UInt8
    public var source: UInt8
    public var expected: UInt32
    public var received: UInt32
    /// The connection interval in 1.25 ms units and the supervision timeout in 10 ms units.
    public var interval: UInt16
    public var timeout: UInt16
    public var eraseMs: UInt32
    public var storeMs: UInt32
    public var flashError: Int32
    /// The name of the sprite playing now.
    public var name: String

    /// nil for fewer than 44 bytes or another version.
    public init?(_ data: Data) {
        guard data.count >= Report.size else { return nil }
        let b = [UInt8](data.prefix(Report.size))
        func u16(_ o: Int) -> UInt16 { UInt16(b[o]) | UInt16(b[o + 1]) << 8 }
        func u32(_ o: Int) -> UInt32 { UInt32(u16(o)) | UInt32(u16(o + 2)) << 16 }
        version = b[0]
        guard version == Report.version else { return nil }
        state = b[1]
        result = b[2]
        source = b[3]
        expected = u32(4)
        received = u32(8)
        interval = u16(12)
        timeout = u16(14)
        eraseMs = u32(16)
        storeMs = u32(20)
        flashError = Int32(bitPattern: u32(24))
        name = String(decoding: b[28..<44].prefix { $0 != 0 }, as: UTF8.self)
    }

    /// The 15 ms interval the device asks the central for.
    public static let fastInterval: UInt16 = 12

    public var isIdle: Bool { state == State.idle.rawValue }
    public var shown: Bool { result == Result.shown.rawValue }

    /// One line for a log. The name is the sprite's subject, so only its length.
    public var line: String {
        let resultName = Result(rawValue: result).map { "\($0)" } ?? "\(result)"
        let sourceName = Source(rawValue: source).map { "\($0)" } ?? "\(source)"
        return "state=\(state) result=\(resultName) source=\(sourceName) expected=\(expected) received=\(received) "
            + "interval=\(Double(interval) * 1.25)ms timeout=\(UInt32(timeout) * 10)ms "
            + "erase_ms=\(eraseMs) store_ms=\(storeMs) flash_err=\(flashError) name_chars=\(name.count)"
    }
}
