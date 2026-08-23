// swift-tools-version:5.9
// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0

import PackageDescription

// Repo root relative to this Package.swift (swift/).
// Sources are compiled in-tree; do not link gitignored build/libsqldeep.a.
let repoRoot = "../"

let package = Package(
    name: "SQLDeepRuntime",
    products: [
        .library(name: "SQLDeepRuntime", targets: ["SQLDeepRuntime"]),
    ],
    dependencies: [
        .package(url: "https://github.com/jpsim/Yams.git", from: "5.0.0"),
    ],
    targets: [
        // Deepparser C TUs (separate from C++ so -std=c++20 is not applied to .c).
        .target(
            name: "CDeepparser",
            path: "Sources/CDeepparser",
            publicHeadersPath: "include",
            cSettings: [
                .unsafeFlags([
                    "-I", repoRoot + "vendor/deepparser/src",
                    "-DNDEBUG",
                    "-Wno-unused-parameter",
                    "-Wno-sign-compare",
                ]),
            ]
        ),
        .target(
            name: "CSQLDeep",
            dependencies: ["CDeepparser"],
            path: "Sources/CSQLDeep",
            publicHeadersPath: "include",
            cxxSettings: [
                .unsafeFlags([
                    "-std=c++20",
                    "-I", repoRoot + "dist",
                    "-I", repoRoot + "vendor/deepparser/src",
                    "-I", repoRoot + "vendor/include",
                ]),
            ],
            linkerSettings: [
                .linkedLibrary("c++"),
            ]
        ),
        // Shared C SQLite runtime (dist/sqldeep_xml.c) — same TU as Go/Kotlin.
        .target(
            name: "CSQLDeepXML",
            path: "Sources/CSQLDeepXML",
            publicHeadersPath: "include",
            cSettings: [
                .unsafeFlags([
                    "-I", repoRoot + "dist",
                    "-I", repoRoot + "vendor/include",
                ]),
            ],
            linkerSettings: [
                .linkedLibrary("sqlite3"),
            ]
        ),
        .target(
            name: "SQLDeepRuntime",
            dependencies: ["CSQLDeep", "CSQLDeepXML"]
        ),
        .testTarget(
            name: "SQLDeepRuntimeTests",
            dependencies: ["SQLDeepRuntime", "Yams"],
            exclude: ["sqlite.yaml"]
        ),
    ]
)
