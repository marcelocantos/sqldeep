// swift-tools-version:5.9
// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0

import PackageDescription

let package = Package(
    name: "SQLDeepRuntime",
    products: [
        .library(name: "SQLDeepRuntime", targets: ["SQLDeepRuntime"]),
    ],
    dependencies: [
        .package(url: "https://github.com/jpsim/Yams.git", from: "5.0.0"),
    ],
    targets: [
        .target(name: "SQLDeepRuntime"),
        .testTarget(
            name: "SQLDeepRuntimeTests",
            dependencies: ["SQLDeepRuntime", "Yams"]
        ),
    ]
)
