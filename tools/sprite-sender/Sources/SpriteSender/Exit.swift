import Foundation

/// 0 the pack plays on the device (or the dry run and the report succeeded),
/// 1 it does not (no device, a refused pack, a drive not mounted: launchd
/// tries again tomorrow), 2 bad usage or config.
enum Exit {
    static func ok() -> Never { exit(0) }
    static func failure(_ message: String) -> Never {
        Log.line("failed: \(message)")
        exit(1)
    }
    static func usage(_ message: String) -> Never {
        FileHandle.standardError.write(Data("sprite-sender: \(message)\n".utf8))
        exit(2)
    }
}
