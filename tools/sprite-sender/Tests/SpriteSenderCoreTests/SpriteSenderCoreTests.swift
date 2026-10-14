import Foundation
import XCTest
@testable import SpriteSenderCore

final class ConfigTests: XCTestCase {
    func testRelativePathsResolveAgainstTheConfigDirectory() throws {
        let base = URL(fileURLWithPath: "/Volumes/X/sprites/")
        let json = Data(#"{"front": "col/front", "back": "col/back", "state": "state"}"#.utf8)
        let c = try SenderConfig(json: json, base: base)
        XCTAssertEqual(c.frontDirectory.path, "/Volumes/X/sprites/col/front")
        XCTAssertEqual(c.backDirectory?.path, "/Volumes/X/sprites/col/back")
        XCTAssertEqual(c.stateFile.path, "/Volumes/X/sprites/state")
    }

    func testBackIsOptionalAndAbsolutePathsStay() throws {
        let json = Data(#"{"front": "/a/front", "state": "/b/state"}"#.utf8)
        let c = try SenderConfig(json: json, base: URL(fileURLWithPath: "/x/"))
        XCTAssertNil(c.backDirectory)
        XCTAssertEqual(c.frontDirectory.path, "/a/front")
        XCTAssertEqual(c.stateFile.path, "/b/state")
    }

    func testBadJSONAndEmptyPathAreErrors() {
        XCTAssertThrowsError(try SenderConfig(json: Data("[]".utf8), base: URL(fileURLWithPath: "/")))
        XCTAssertThrowsError(try SenderConfig(json: Data(#"{"front": "", "state": "s"}"#.utf8), base: URL(fileURLWithPath: "/"))) {
            XCTAssertEqual($0 as? SenderConfig.Error, .emptyPath("front"))
        }
    }

    func testRangeIsFirstTildeLastOrOpenAndNeedsTheOrder() throws {
        let base = URL(fileURLWithPath: "/x/")
        func config(range: String, order: Bool = true) throws -> SenderConfig {
            let orderKey = order ? #", "order": "o""# : ""
            let json = #"{"front": "f", "state": "s""# + orderKey + #", "range": "# + range + "}"
            return try SenderConfig(json: Data(json.utf8), base: base)
        }
        XCTAssertEqual(try config(range: #""1 ~ 151""#).range, Rotation.PlaceRange(first: 1, last: 151))
        XCTAssertEqual(try config(range: #""152~200""#).range, Rotation.PlaceRange(first: 152, last: 200))
        XCTAssertEqual(try config(range: #"" 100 ~ ""#).range, Rotation.PlaceRange(first: 100, last: nil))
        XCTAssertEqual(try config(range: #""7 ~ 7""#).range, Rotation.PlaceRange(first: 7, last: 7))
        XCTAssertNil(try SenderConfig(json: Data(#"{"front": "f", "state": "s", "order": "o"}"#.utf8), base: base).range)
        for bad in ["1, 3, 5", "151", "~ 151", "0 ~ 5", "151 ~ 1", "1 ~ 151 ~ 200", "-1 ~ 5", "+1 ~ 5", "1 〜 151", "１ ~ １５１", ""] {
            XCTAssertThrowsError(try config(range: "\"\(bad)\""), bad) {
                XCTAssertEqual($0 as? SenderConfig.Error, .badRange(bad))
            }
        }
        XCTAssertThrowsError(try config(range: "[1, 151]"), "a string, not a list")
        XCTAssertThrowsError(try config(range: #""1 ~ 151""#, order: false)) {
            XCTAssertEqual($0 as? SenderConfig.Error, .rangeWithoutOrder)
        }
    }
}

final class RotationTests: XCTestCase {
    let entries = ["b.gif", "._a.gif", "README.md", "a.gif", "C.GIF", "notes.txt"]

    func testCandidatesAreGIFsInByteOrderWithoutSidecars() {
        XCTAssertEqual(Rotation.candidates(entries), ["C.GIF", "a.gif", "b.gif"])
    }

    func testOrderListsFirstThenTheRestInByteOrder() {
        let order = Rotation.order(in: Data("b.gif\r\n\nzz.gif\nb.gif\nC.GIF\n".utf8))
        XCTAssertEqual(order, ["b.gif", "zz.gif", "C.GIF"], "one a line, once each")
        let c = Rotation.candidates(entries, order: order)
        XCTAssertEqual(c, ["b.gif", "C.GIF", "a.gif"], "listed in order, missing skipped, the rest after")
        XCTAssertEqual(Rotation.next(after: "a.gif", in: c, order: order), "b.gif")
        XCTAssertEqual(Rotation.next(after: "zz.gif", in: c, order: order), "C.GIF", "a gone listed name continues down the list")
        XCTAssertEqual(Rotation.next(after: "a.gif~", in: c, order: order), "b.gif", "a gone unlisted name past the unlisted ones wraps")
    }

    func testRangeKeepsOnlyItsPlacesAndWrapsToItsFirst() throws {
        let entries = ["a.gif", "b.gif", "d.gif", "e.gif", "x.gif", "._b.gif"]
        let order = ["a.gif", "b.gif", "gone.gif", "d.gif", "e.gif"]
        let range = try XCTUnwrap(Rotation.PlaceRange("2 ~ 4"))
        let c = Rotation.candidates(entries, order: order, range: range)
        XCTAssertEqual(c, ["b.gif", "d.gif"], "places 2 to 4: a gone name skipped, the unlisted left out")
        XCTAssertEqual(Rotation.next(after: "d.gif", in: c, order: order), "b.gif", "after the last place, the first")
        XCTAssertEqual(Rotation.next(after: "b.gif", in: c, order: order), "d.gif")
        XCTAssertEqual(Rotation.next(after: "a.gif", in: c, order: order), "b.gif", "a state before the range enters it")
        XCTAssertEqual(Rotation.next(after: "e.gif", in: c, order: order), "b.gif", "a state after the range starts it over")
        XCTAssertEqual(Rotation.next(after: "gone.gif", in: c, order: order), "d.gif", "a gone name inside goes on down the list")
        XCTAssertEqual(Rotation.next(after: "x.gif", in: c, order: order), "b.gif", "an unlisted state starts it over")
        XCTAssertEqual(Rotation.next(after: nil, in: c, order: order), "b.gif")

        let open = try XCTUnwrap(Rotation.PlaceRange("4 ~"))
        XCTAssertEqual(Rotation.candidates(entries, order: order, range: open), ["d.gif", "e.gif"], "to the order's end")
        XCTAssertEqual(Rotation.candidates(entries, order: order, range: Rotation.PlaceRange("2 ~ 99")), ["b.gif", "d.gif", "e.gif"])
        XCTAssertEqual(Rotation.candidates(entries, order: order, range: Rotation.PlaceRange("6 ~")), [])
        XCTAssertEqual(
            Rotation.candidates(entries, order: ["a.gif", "a.gif", "b.gif"], range: Rotation.PlaceRange("2 ~ 2")),
            ["b.gif"], "a name listed twice holds one place")
        XCTAssertEqual("\(range) / \(open)", "2 ~ 4 / 4 ~", "as the log prints it")
    }

    func testNextWrapsAndStartsOverOnUnknownState() {
        let c = Rotation.candidates(entries)
        XCTAssertEqual(Rotation.next(after: nil, in: c), "C.GIF")
        XCTAssertEqual(Rotation.next(after: "C.GIF", in: c), "a.gif")
        XCTAssertEqual(Rotation.next(after: "b.gif", in: c), "C.GIF")
        XCTAssertEqual(Rotation.next(after: "a.gif~", in: c), "b.gif", "a gone name continues after its place")
        XCTAssertEqual(Rotation.next(after: "z.gif", in: c), "C.GIF", "a gone name past the end wraps")
        XCTAssertNil(Rotation.next(after: nil, in: []))
    }

    func testStateRoundTripKeepsEveryByteOfTheName() {
        for name in ["a b.gif", "  Nidoran F .GIF", "trailing .gif"] {
            XCTAssertEqual(Rotation.name(inState: Rotation.state(for: name)), name)
        }
        XCTAssertEqual(Rotation.name(inState: Data("x.gif\nignored\n".utf8)), "x.gif")
        XCTAssertEqual(Rotation.name(inState: Data("x.gif\r\n".utf8)), "x.gif")
        XCTAssertNil(Rotation.name(inState: Data()))
        XCTAssertNil(Rotation.name(inState: Data("\n".utf8)))
    }
}

final class SpriteNameTests: XCTestCase {
    func testRuleOfSpriteNameSh() throws {
        XCTAssertEqual(try SpriteName.from(fileName: "my sprite.gif"), "My sprite")
        XCTAssertEqual(try SpriteName.from(fileName: "  Nidoran F .GIF"), "Nidoran F")
        XCTAssertEqual(try SpriteName.from(fileName: "Farfetch'd.gif"), "Farfetch'd")
        XCTAssertEqual(try SpriteName.from(fileName: "abcdefghijklmnopqrs.gif"), "Abcdefghijklmno")
        XCTAssertEqual(try SpriteName.from(fileName: "abcdefghijklmn opq.gif"), "Abcdefghijklmn", "the cut leaves no trailing space")
        XCTAssertEqual(try SpriteName.from(fileName: "noext"), "Noext")
    }

    func testRejectsEmptyAndNonASCII() {
        XCTAssertThrowsError(try SpriteName.from(fileName: ".gif")) { XCTAssertEqual($0 as? SpriteName.Error, .empty) }
        XCTAssertThrowsError(try SpriteName.from(fileName: "   .gif")) { XCTAssertEqual($0 as? SpriteName.Error, .empty) }
        XCTAssertEqual(try SpriteName.from(fileName: ".hidden.gif"), ".hidden", "as sprite-name.sh: only the last extension goes")
        XCTAssertThrowsError(try SpriteName.from(fileName: "Nidoran♀.gif")) {
            XCTAssertEqual($0 as? SpriteName.Error, .notPrintableASCII)
        }
    }
}

final class ReportTests: XCTestCase {
    func testDecodesVersionOne() {
        var b = [UInt8](repeating: 0, count: 44)
        b[0] = 1; b[1] = 0; b[2] = 1; b[3] = 2
        b[4] = 0x2c; b[5] = 0x26; b[6] = 0x03          // expected 206_380
        b[8] = 0x2c; b[9] = 0x26; b[10] = 0x03
        b[12] = 12; b[14] = 0x90; b[15] = 0x01          // interval 12, timeout 400
        b[16] = 0x5e; b[17] = 0x2b                      // erase 11102
        b[20] = 0x10; b[21] = 0x27                      // store 10000
        b[24] = 0xfe; b[25] = 0xff; b[26] = 0xff; b[27] = 0xff  // -2
        for (i, c) in "Nidoran F".utf8.enumerated() { b[28 + i] = c }
        let r = Report(Data(b))!
        XCTAssertTrue(r.isIdle && r.shown)
        XCTAssertEqual(r.source, Report.Source.room.rawValue)
        XCTAssertEqual(r.expected, 206_380)
        XCTAssertEqual(r.received, 206_380)
        XCTAssertEqual(r.interval, 12)
        XCTAssertEqual(r.timeout, 400)
        XCTAssertEqual(r.eraseMs, 11102)
        XCTAssertEqual(r.storeMs, 10000)
        XCTAssertEqual(r.flashError, -2)
        XCTAssertEqual(r.name, "Nidoran F")
        XCTAssertFalse(r.line.contains("Nidoran"))
        XCTAssertTrue(r.line.contains("name_chars=9"))
    }

    func testRejectsShortAndOtherVersions() {
        XCTAssertNil(Report(Data(repeating: 1, count: 43)))
        var b = [UInt8](repeating: 0, count: 44)
        b[0] = 2
        XCTAssertNil(Report(Data(b)))
    }
}

final class TransferTests: XCTestCase {
    func testBeginIsLittleEndian() {
        XCTAssertEqual(Transfer.begin(length: 0x0003_262c), [0x01, 0x2c, 0x26, 0x03, 0x00])
    }

    func testPlanCommitsAfterEveryFullPageButTheLast() {
        let plan = Transfer.plan(count: 4096 + 100, chunk: 512)
        XCTAssertEqual(plan.count, 8 + 1 + 1)
        XCTAssertEqual(plan[7], .write(3584..<4096))
        XCTAssertEqual(plan[8], .commit)
        XCTAssertEqual(plan[9], .write(4096..<4196))
        XCTAssertEqual(Transfer.plan(count: 4096, chunk: 4096), [.write(0..<4096)])
        XCTAssertEqual(Transfer.plan(count: 8192, chunk: 4096), [.write(0..<4096), .commit, .write(4096..<8192)])
    }

    /// The device's side: one page buffer, filled by the data writes, written
    /// by COMMIT (a full page, a multiple of the write block) and by END.
    func testPlanFillsTheDevicePageExactly() {
        for (count, chunk) in [(10_000, 509), (206_444, 182), (4096, 20), (1, 4), (8193, 4096)] {
            var fill = 0, offset = 0, commits = 0
            for step in Transfer.plan(count: count, chunk: chunk) {
                switch step {
                case .write(let r):
                    XCTAssertEqual(r.lowerBound, offset)
                    XCTAssertLessThanOrEqual(fill + r.count, 4096, "a write overflows the page")
                    fill += r.count
                    offset = r.upperBound
                case .commit:
                    XCTAssertEqual(fill, 4096, "COMMIT before the page is full")
                    fill = 0
                    commits += 1
                }
            }
            XCTAssertEqual(offset, count)
            XCTAssertEqual(commits, (count - 1) / 4096)
            XCTAssertEqual(fill, count - commits * 4096, "what END commits")
            XCTAssertGreaterThan(fill, 0)
        }
        XCTAssertEqual(Transfer.plan(count: 0, chunk: 512), [])
    }
}
