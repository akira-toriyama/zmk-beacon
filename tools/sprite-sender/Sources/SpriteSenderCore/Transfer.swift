import Foundation

/// The transfer as src/ble_receiver.c takes it: BEGIN with the pack's length,
/// the bytes on the data characteristic with COMMIT after every page (the
/// device holds one page and writes it to flash on COMMIT, and the central
/// sends nothing until the response), END.
public enum Transfer {
    public static let page = 4096
    /// A COMMIT's bytes must be a multiple of the flash write block; a chunk
    /// that is one keeps every page boundary aligned without a split write.
    public static let writeBlock = 4

    public static func begin(length: Int) -> [UInt8] {
        let n = UInt32(length)
        return [0x01, UInt8(n & 0xff), UInt8(n >> 8 & 0xff), UInt8(n >> 16 & 0xff), UInt8(n >> 24)]
    }
    public static let commit: [UInt8] = [0x02]
    public static let end: [UInt8] = [0x03]

    public enum Step: Equatable, Sendable {
        /// Write these bytes of the pack to the data characteristic.
        case write(Range<Int>)
        /// Write COMMIT to the control characteristic and wait for its response.
        case commit
    }

    /// The writes and commits for a pack of `count` bytes with writes of at
    /// most `chunk` bytes (what CoreBluetooth allows without response, cut to
    /// a multiple of the write block): a write never crosses a page, and a
    /// COMMIT follows every full page but the last partial or full one, which
    /// END commits.
    public static func plan(count: Int, chunk: Int) -> [Step] {
        guard count > 0, chunk >= writeBlock else { return [] }
        let chunk = min(chunk - chunk % writeBlock, page)
        var steps: [Step] = []
        var offset = 0
        while offset < count {
            let pageEnd = min((offset / page + 1) * page, count)
            while offset < pageEnd {
                let end = min(offset + chunk, pageEnd)
                steps.append(.write(offset..<end))
                offset = end
            }
            if offset < count {
                steps.append(.commit)
            }
        }
        return steps
    }
}
