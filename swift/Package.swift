// swift-tools-version:5.9
// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0

import PackageDescription

let package = Package(
    name: "SQLDeepRuntime",
    products: [
        .library(name: "SQLDeepRuntime", targets: ["SQLDeepRuntime"]),
    ],
    targets: [
        .target(name: "SQLDeepRuntime"),
    ]
)
