// swift-tools-version:5.9
// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0

import PackageDescription

let repoRoot = "../"
let buildDir = repoRoot + "build"

let package = Package(
    name: "SQLDeepRuntime",
    products: [
        .library(name: "SQLDeepRuntime", targets: ["SQLDeepRuntime"]),
    ],
    dependencies: [
        .package(url: "https://github.com/jpsim/Yams.git", from: "5.0.0"),
    ],
    targets: [
        .target(
            name: "CSQLDeep",
            path: "Sources/CSQLDeep",
            publicHeadersPath: "include"
        ),
        .target(
            name: "SQLDeepRuntime",
            dependencies: ["CSQLDeep"],
            linkerSettings: [
                .unsafeFlags([
                    buildDir + "/libsqldeep.a",
                    buildDir + "/sqldeep_xml.o",
                    "-lstdc++",
                    "-lz",
                ]),
            ]
        ),
        .testTarget(
            name: "SQLDeepRuntimeTests",
            dependencies: ["SQLDeepRuntime", "Yams"],
            exclude: ["sqlite.yaml"]
        ),
    ]
)
