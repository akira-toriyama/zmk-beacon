// swift-tools-version:6.0
//
// sprite-sender: the Mac side of the Prospector Dongle's daily sprite swap.
//
//   SpriteSenderCore  pure: the config file, the rotation over the GIF
//                     directory, the sprite name rule, the receiver's report
//                     and the transfer's page plan. No Foundation I/O beyond
//                     Data and URL arithmetic, no CoreBluetooth: what the
//                     tests cover.
//   SpriteSender      the executable: argv, files, the pack converter as a
//                     child process (tools/sprite_pack.py), CoreBluetooth.
//
// No dependencies: the Prospector Dongle's protocol is 3 opcodes and the
// config is one JSON object, so Foundation suffices.

import PackageDescription

let package = Package(
    name: "sprite-sender",
    platforms: [.macOS("26.0")],
    products: [
        .executable(name: "sprite-sender", targets: ["SpriteSender"]),
    ],
    targets: [
        .target(name: "SpriteSenderCore"),
        .executableTarget(
            name: "SpriteSender", dependencies: ["SpriteSenderCore"],
            // CoreBluetooth's delegate types are not Sendable; its callbacks
            // all land on the main queue here (Sender.swift).
            swiftSettings: [.swiftLanguageMode(.v5)]),
        .testTarget(name: "SpriteSenderCoreTests", dependencies: ["SpriteSenderCore"]),
    ]
)
