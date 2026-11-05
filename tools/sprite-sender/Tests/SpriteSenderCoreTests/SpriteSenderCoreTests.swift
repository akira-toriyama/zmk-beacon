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

    func testRangeIsFirstTildeLastOrOpen() throws {
        let base = URL(fileURLWithPath: "/x/")
        func config(range: String) throws -> SenderConfig {
            try SenderConfig(json: Data((#"{"front": "f", "state": "s", "range": "# + range + "}").utf8), base: base)
        }
        XCTAssertEqual(try config(range: #""1 ~ 151""#).range, Rotation.NumberRange(first: 1, last: 151))
        XCTAssertEqual(try config(range: #""152~200""#).range, Rotation.NumberRange(first: 152, last: 200))
        XCTAssertEqual(try config(range: #"" 100 ~ ""#).range, Rotation.NumberRange(first: 100, last: nil))
        XCTAssertEqual(try config(range: #""7 ~ 7""#).range, Rotation.NumberRange(first: 7, last: 7))
        XCTAssertNil(try SenderConfig(json: Data(#"{"front": "f", "state": "s"}"#.utf8), base: base).range)
        for bad in ["1, 3, 5", "151", "~ 151", "0 ~ 5", "151 ~ 1", "1 ~ 151 ~ 200", "-1 ~ 5", "+1 ~ 5", "1 〜 151", "１ ~ １５１", ""] {
            XCTAssertThrowsError(try config(range: "\"\(bad)\""), bad) {
                XCTAssertEqual($0 as? SenderConfig.Error, .badRange)
            }
        }
        XCTAssertThrowsError(try config(range: "[1, 151]"), "a string, not a list")
    }

    func testNamesResolvesLikeTheOthers() throws {
        let base = URL(fileURLWithPath: "/x/")
        let c = try SenderConfig(json: Data(#"{"front": "f", "state": "s", "names": "names.csv"}"#.utf8), base: base)
        XCTAssertEqual(c.namesFile?.path, "/x/names.csv")
        XCTAssertNil(try SenderConfig(json: Data(#"{"front": "f", "state": "s"}"#.utf8), base: base).namesFile)
        XCTAssertThrowsError(try SenderConfig(json: Data(#"{"front": "f", "state": "s", "names": ""}"#.utf8), base: base)) {
            XCTAssertEqual($0 as? SenderConfig.Error, .emptyPath("names"))
        }
    }

    func testOrderIsAConfigError() {
        let base = URL(fileURLWithPath: "/x/")
        for json in [#"{"front": "f", "state": "s", "order": "o"}"#, #"{"front": "f", "state": "s", "order": "o", "range": "1 ~ 151", "names": "n"}"#, #"{"front": "f", "state": "s", "order": ""}"#] {
            XCTAssertThrowsError(try SenderConfig(json: Data(json.utf8), base: base), json) {
                XCTAssertEqual($0 as? SenderConfig.Error, .orderIsGone, json)
            }
        }
    }
}

final class RotationTests: XCTestCase {
    func testANumberIsAPositiveIntegerAsSwiftWritesIt() {
        for (name, number) in [("1.gif", 1), ("151.gif", 151), ("649.gif", 649), ("10001.gif", 10001)] {
            XCTAssertEqual(Rotation.number(ofFileName: name), number, name)
            XCTAssertEqual(Rotation.fileName(of: number), name)
        }
        for name in ["0.gif", "-1.gif", "+1.gif", "01.gif", "1-mega.gif", "1.GIF", "1.gif.gif", ".gif", "._1.gif", "a.gif",
                     "1", "1.png", "１.gif", "1 .gif", " 1.gif", "1_000.gif", "99999999999999999999.gif", "shiny", "female"] {
            XCTAssertNil(Rotation.number(ofFileName: name), name)
        }
    }

    func testCandidatesAreTheNumbersAscendingNotInByteOrder() {
        let entries = ["10.gif", "9.gif", "._9.gif", "1.gif", "9-form.gif", "name.gif", "back", "notes.txt", "100.gif"]
        XCTAssertEqual(Rotation.candidates(entries), [1, 9, 10, 100])
        XCTAssertEqual(Rotation.candidates([]), [])
    }

    func testRangeKeepsOnlyItsNumbers() throws {
        let entries = ["1.gif", "2.gif", "4.gif", "5.gif", "x.gif"]
        XCTAssertEqual(Rotation.candidates(entries, range: Rotation.NumberRange("2 ~ 4")), [2, 4], "a number without a file skipped")
        XCTAssertEqual(Rotation.candidates(entries, range: Rotation.NumberRange("4 ~")), [4, 5], "open: to the last number")
        XCTAssertEqual(Rotation.candidates(entries, range: Rotation.NumberRange("2 ~ 99")), [2, 4, 5])
        XCTAssertEqual(Rotation.candidates(entries, range: Rotation.NumberRange("6 ~")), [])
        let range = try XCTUnwrap(Rotation.NumberRange("2 ~ 4"))
        let open = try XCTUnwrap(Rotation.NumberRange("4 ~"))
        XCTAssertEqual("\(range) / \(open)", "2 ~ 4 / 4 ~", "as the config writes it")
        XCTAssertTrue(range.contains(2) && range.contains(4) && !range.contains(1) && !range.contains(5))
        XCTAssertTrue(open.contains(4) && open.contains(1_000_000) && !open.contains(3))
    }

    func testNextGoesUpAndWraps() {
        let c = [2, 4, 7]
        XCTAssertEqual(Rotation.next(after: nil, in: c), 2, "without a state, the first")
        XCTAssertEqual(Rotation.next(after: 2, in: c), 4)
        XCTAssertEqual(Rotation.next(after: 4, in: c), 7)
        XCTAssertEqual(Rotation.next(after: 7, in: c), 2, "after the last, the first")
        XCTAssertEqual(Rotation.next(after: 1, in: c), 2, "a state before the range enters it")
        XCTAssertEqual(Rotation.next(after: 3, in: c), 4, "a gone number goes on from its place")
        XCTAssertEqual(Rotation.next(after: 8, in: c), 2, "a state past the end starts over")
        XCTAssertEqual(Rotation.next(after: 5, in: [5]), 5, "one GIF, again")
        XCTAssertNil(Rotation.next(after: nil, in: []))
        XCTAssertNil(Rotation.next(after: 3, in: []))
    }

    func testStateRoundTripAndWhatIsNoState() {
        for number in [1, 151, 10001] {
            XCTAssertEqual(Rotation.number(inState: Rotation.state(for: number)), number)
        }
        XCTAssertEqual(Rotation.state(for: 6), Data("6\n".utf8), "one line")
        XCTAssertEqual(Rotation.number(inState: Data("12\nignored\n".utf8)), 12)
        XCTAssertEqual(Rotation.number(inState: Data("12\r\n".utf8)), 12)
        XCTAssertEqual(Rotation.number(inState: Data("12".utf8)), 12)
        for bad in ["", "\n", "0\n", "-3\n", "012\n", "12 \n", " 12\n", "name.gif\n", "12.gif\n", "\n12\n"] {
            XCTAssertNil(Rotation.number(inState: Data(bad.utf8)), bad)
        }
    }
}

final class SpriteNameTests: XCTestCase {
    func testTheToolsLineIsTheNameAsItsBytesAre() {
        XCTAssertEqual(SpriteName.choose(number: 1, toolOutput: Data("テストテス\n".utf8)),
                       .init(name: "テストテス", source: .katakana))
        XCTAssertEqual(SpriteName.choose(number: 1, toolOutput: Data("Mr. Test\n".utf8)),
                       .init(name: "Mr. Test", source: .ascii), "not cut at its dot, as a file name would be")
        XCTAssertEqual(SpriteName.choose(number: 1, toolOutput: Data("テスト\nnoise\n".utf8)).name, "テスト", "the first line")
        XCTAssertEqual(SpriteName.choose(number: 1, toolOutput: Data("テスト".utf8)).name, "テスト", "no newline")
        XCTAssertEqual(Array(SpriteName.choose(number: 1, toolOutput: Data("ガピ\n".utf8)).name.utf8).count, 6,
                       "composed in bytes too: String's == takes NFD for NFC")
    }

    func testTheNumberIsTheLastResort() {
        XCTAssertEqual(SpriteName.choose(number: 6, toolOutput: nil), .init(name: "6", source: .ascii), "no CSV in the config")
        XCTAssertEqual(SpriteName.choose(number: 10001, toolOutput: Data("\n".utf8)), .init(name: "10001", source: .ascii), "no name in the CSV")
        XCTAssertEqual(SpriteName.choose(number: 6, toolOutput: Data()), .init(name: "6", source: .ascii))
        XCTAssertEqual(SpriteName.choose(number: Int.max, toolOutput: nil).name.utf8.count, SpriteName.maxBytes, "a pack holds 15 bytes")
    }

    func testTheLogSaysTheAlphabetNeverWhichASCII() {
        XCTAssertEqual("\(SpriteName.Source.katakana)", "katakana from the name dictionary")
        XCTAssertEqual("\(SpriteName.Source.ascii)", "ASCII")
    }

    func testKatakanaRangesMatchSpritePack() {
        XCTAssertEqual(SpriteName.katakanaRanges.reduce(0) { $0 + $1.count }, 131, "sprite_pack.KATAKANA_RANGES: 2 + 92 + 11 + 26")
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
        for (i, c) in "Test one".utf8.enumerated() { b[28 + i] = c }
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
        XCTAssertEqual(r.name, "Test one")
        XCTAssertFalse(r.line.contains("Test"))
        XCTAssertEqual(r.line, "state=0 result=shown source=room expected=206380 received=206380 interval=15.0ms "
                       + "timeout=4000ms erase_ms=11102 store_ms=10000 flash_err=-2 name=ascii",
                       "the whole line: the alphabet, never the length")
        func named(_ text: String) -> Report {
            for i in 28..<44 { b[i] = 0 }
            for (i, c) in text.utf8.enumerated() { b[28 + i] = c }
            return Report(Data(b))!
        }
        let katakana = named("テストテス")
        XCTAssertEqual(katakana.name, "テストテス")
        XCTAssertTrue(katakana.line.hasSuffix(" name=katakana"))
        XCTAssertFalse(katakana.line.contains("テ"))
        XCTAssertEqual(named("ア♂Ｚ：２").nameKind, "katakana", "the font's signs, capitals and digits")
        XCTAssertEqual(named("").nameKind, "none")
        XCTAssertEqual(named("Aテ").nameKind, "other", "neither font alone")
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
