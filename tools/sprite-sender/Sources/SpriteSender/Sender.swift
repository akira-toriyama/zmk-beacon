import CoreBluetooth
import Foundation
import SpriteSenderCore

/// One connection to the Prospector Dongle's sprite receiver (zmk-beacon
/// src/ble_receiver.c, the protocol in its header): find it by its service
/// UUID, read the report, wait for the 15 ms interval the device asks for,
/// BEGIN / pages with COMMIT / END, read the report, disconnect. Runs on the
/// main queue (CoreBluetooth's delegate queue nil) under dispatchMain(), and
/// ends the process: there is one transfer a run. The target compiles in
/// Swift 5 language mode (Package.swift): CoreBluetooth's delegate types are
/// not Sendable, and every callback lands on the one queue anyway.
final class Sender: NSObject {
    static let serviceUUID = CBUUID(string: "B5E1C0DE-7A3F-4C1B-9D2E-0F51A9C3E700")
    static let controlUUID = CBUUID(string: "B5E1C0DE-7A3F-4C1B-9D2E-0F51A9C3E701")
    static let dataUUID = CBUUID(string: "B5E1C0DE-7A3F-4C1B-9D2E-0F51A9C3E702")

    /// The first run waits for the user to answer macOS's Bluetooth
    /// permission dialog; the scan deadline runs from poweredOn, after it.
    static let authorizationSeconds = 600.0
    /// A device that advertises once a second is found within a few seconds;
    /// one that is off never is. A 206 KB pack took 20 s (hardware 2026-10-12).
    static let scanSeconds = 30.0
    static let transferSeconds = 120.0
    /// The device asks for the interval a second after connecting and a Mac
    /// applies it within another (hardware 2026-10-12); the transfer runs at
    /// the slow interval when it never comes.
    static let intervalTries = 10
    static let intervalPollSeconds = 0.5

    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var control: CBCharacteristic?
    private var data: CBCharacteristic?
    private let payload: Data?
    private var steps: [Transfer.Step] = []
    private var sending = false
    private var finished = false
    private var onWrite: (() -> Void)?
    private var onReport: ((Report) -> Void)?
    private var started = Date()
    private var beginAt = Date()
    private var deadline: DispatchWorkItem?

    /// nil: connect, read the report, print it, leave.
    init(payload: Data?) {
        self.payload = payload
        super.init()
    }

    func run() -> Never {
        let auth = CBCentralManager.authorization
        if auth == .denied || auth == .restricted {
            Exit.failure("Bluetooth is not allowed for this app (System Settings > Privacy & Security > Bluetooth)")
        }
        Log.line("start bytes=\(payload?.count ?? 0)")
        central = CBCentralManager(delegate: self, queue: nil)
        arm(seconds: Self.authorizationSeconds, "Bluetooth did not become available (permission dialog unanswered?)")
        dispatchMain()
    }

    private func arm(seconds: Double, _ message: String) {
        deadline?.cancel()
        let item = DispatchWorkItem { Exit.failure("timeout after \(Int(seconds)) s: \(message)") }
        deadline = item
        DispatchQueue.main.asyncAfter(deadline: .now() + seconds, execute: item)
    }

    private func write(_ bytes: [UInt8], then: @escaping () -> Void) {
        onWrite = then
        peripheral!.writeValue(Data(bytes), for: control!, type: .withResponse)
    }

    private func read(then: @escaping (Report) -> Void) {
        onReport = then
        peripheral!.readValue(for: control!)
    }

    private func leave(code: Int32) {
        finished = true
        deadline?.cancel()
        if let peripheral {
            central.cancelPeripheralConnection(peripheral)
            // didDisconnect ends the process; a late disconnect must not hang it.
            DispatchQueue.main.asyncAfter(deadline: .now() + 2) { exit(code) }
        } else {
            exit(code)
        }
    }

    private var exitCode: Int32 = 1

    // MARK: the transfer

    private func connected() {
        read { r in
            Log.line("report at connect: \(r.line)")
            guard let payload = self.payload else {
                self.exitCode = 0
                self.leave(code: 0)
                return
            }
            guard r.isIdle else {
                Exit.failure("the device is in a transfer")
            }
            self.steps = Transfer.plan(count: payload.count,
                                       chunk: self.peripheral!.maximumWriteValueLength(for: .withoutResponse))
            self.awaitInterval(tries: Self.intervalTries)
        }
    }

    private func awaitInterval(tries: Int) {
        DispatchQueue.main.asyncAfter(deadline: .now() + Self.intervalPollSeconds) { [self] in
            read { r in
                if r.interval <= Report.fastInterval || tries <= 1 {
                    Log.line("interval \(Double(r.interval) * 1.25) ms after \(self.elapsed(self.started)) s")
                    self.begin()
                } else {
                    self.awaitInterval(tries: tries - 1)
                }
            }
        }
    }

    private func begin() {
        arm(seconds: Self.transferSeconds, "the transfer did not finish")
        beginAt = Date()
        write(Transfer.begin(length: payload!.count)) {
            Log.line("begin answered (room erased) after \(self.elapsed(self.beginAt)) s")
            self.sending = true
            self.pump()
        }
    }

    /// Runs the plan: data writes while CoreBluetooth takes them, a COMMIT as
    /// a write with response that pauses the data until it answers, and END,
    /// queued behind the last data write, answered once the device showed or
    /// refused the pack.
    private func pump() {
        guard sending, let peripheral, let data, let payload else { return }
        while let step = steps.first {
            switch step {
            case .write(let range):
                guard peripheral.canSendWriteWithoutResponse else { return }
                peripheral.writeValue(payload.subdata(in: range), for: data, type: .withoutResponse)
                steps.removeFirst()
            case .commit:
                steps.removeFirst()
                sending = false
                write(Transfer.commit) {
                    self.sending = true
                    self.pump()
                }
                return
            }
        }
        sending = false
        write(Transfer.end) {
            self.read { r in
                let seconds = self.elapsed(self.beginAt)
                Log.line("end answered: \(payload.count) bytes in \(seconds) s from begin; report: \(r.line)")
                if r.shown && r.isIdle {
                    self.exitCode = 0
                    self.leave(code: 0)
                } else {
                    Log.line("failed: the device did not show the pack")
                    self.leave(code: 1)
                }
            }
        }
    }

    private func elapsed(_ since: Date) -> String {
        String(format: "%.2f", -since.timeIntervalSinceNow)
    }
}

extension Sender: CBCentralManagerDelegate, CBPeripheralDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn:
            arm(seconds: Self.scanSeconds, "no Prospector Dongle found")
            central.scanForPeripherals(withServices: [Self.serviceUUID])
        case .unauthorized:
            Exit.failure("Bluetooth is not allowed for this app (System Settings > Privacy & Security > Bluetooth)")
        case .poweredOff:
            Exit.failure("Bluetooth is off")
        case .unsupported:
            Exit.failure("this Mac has no Bluetooth")
        default:
            break
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                                    advertisementData: [String: Any], rssi RSSI: NSNumber) {
        guard self.peripheral == nil else { return }
        Log.line("found rssi=\(RSSI) after \(elapsed(started)) s")
        self.peripheral = peripheral
        peripheral.delegate = self
        central.stopScan()
        central.connect(peripheral)
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        Log.line("connected after \(elapsed(started)) s")
        peripheral.discoverServices([Self.serviceUUID])
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        Exit.failure("connect failed: \(error.map { "\($0)" } ?? "no error")")
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        if finished {
            exit(exitCode)
        }
        Exit.failure("disconnected during the transfer: \(error.map { "\($0)" } ?? "no error")")
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard let service = peripheral.services?.first(where: { $0.uuid == Self.serviceUUID }) else {
            Exit.failure("the device has no sprite receiver service: \(error.map { "\($0)" } ?? "none advertised")")
        }
        peripheral.discoverCharacteristics([Self.controlUUID, Self.dataUUID], for: service)
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        control = service.characteristics?.first { $0.uuid == Self.controlUUID }
        data = service.characteristics?.first { $0.uuid == Self.dataUUID }
        guard control != nil, data != nil else {
            Exit.failure("the sprite receiver service lacks its characteristics: \(error.map { "\($0)" } ?? "")")
        }
        connected()
    }

    func peripheralIsReady(toSendWriteWithoutResponse peripheral: CBPeripheral) {
        pump()
    }

    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        if let error {
            Exit.failure("the device rejected a control write: \(error)")
        }
        let then = onWrite
        onWrite = nil
        then?()
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        guard let value = characteristic.value, let report = Report(value) else {
            Exit.failure("no report of version \(Report.version) from the device (\(characteristic.value?.count ?? 0) bytes): \(error.map { "\($0)" } ?? "")")
        }
        let then = onReport
        onReport = nil
        then?(report)
    }
}
