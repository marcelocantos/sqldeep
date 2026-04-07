// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0
//
// Pure Swift port of sqldeep runtime functions for SQLite.
//
// BLOB protocol: all structured values (XML, JSONML, JSX, JSON) are returned
// as BLOBs. BLOBs starting with '<' are XML; all others are JSON. This lets
// values survive through views, CTEs, and subqueries without losing type info.
//
// Usage:
//   import SQLDeepRuntime
//   let db: OpaquePointer = ...  // sqlite3*
//   sqldeepRegisterSQLite(db)

import Foundation
#if canImport(SQLite3)
import SQLite3
#elseif canImport(CSQLite)
import CSQLite
#endif

// MARK: - Public API

/// Register all sqldeep runtime functions on a SQLite connection.
/// Returns SQLITE_OK on success.
@discardableResult
public func sqldeepRegisterSQLite(_ db: OpaquePointer) -> Int32 {
    var rc: Int32 = SQLITE_OK

    // XML functions
    rc = registerScalar(db, "xml_element", -1, xmlElement)
    guard rc == SQLITE_OK else { return rc }
    rc = registerScalar(db, "xml_attrs", -1, xmlAttrs)
    guard rc == SQLITE_OK else { return rc }
    rc = registerAggregate(db, "xml_agg", 1, xmlAggStep, xmlAggFinal)
    guard rc == SQLITE_OK else { return rc }

    // JSONML functions
    rc = registerScalar(db, "xml_element_jsonml", -1, xmlElementJsonml)
    guard rc == SQLITE_OK else { return rc }
    rc = registerScalar(db, "xml_attrs_jsonml", -1, xmlAttrsJsonml)
    guard rc == SQLITE_OK else { return rc }
    rc = registerAggregate(db, "jsonml_agg", 1, jsonmlAggStep, jsonmlAggFinal)
    guard rc == SQLITE_OK else { return rc }

    // JSX functions (element reuses JSONML impl)
    rc = registerScalar(db, "xml_element_jsx", -1, xmlElementJsonml)
    guard rc == SQLITE_OK else { return rc }
    rc = registerScalar(db, "xml_attrs_jsx", -1, xmlAttrsJsx)
    guard rc == SQLITE_OK else { return rc }
    rc = registerAggregate(db, "jsx_agg", 1, jsonmlAggStep, jsonmlAggFinal)
    guard rc == SQLITE_OK else { return rc }

    // Custom JSON functions
    rc = registerScalar(db, "sqldeep_json", 1, sqldeepJson)
    guard rc == SQLITE_OK else { return rc }
    rc = registerScalar(db, "sqldeep_json_object", -1, sqldeepJsonObject)
    guard rc == SQLITE_OK else { return rc }
    rc = registerScalar(db, "sqldeep_json_array", -1, sqldeepJsonArray)
    guard rc == SQLITE_OK else { return rc }
    rc = registerAggregate(db, "sqldeep_json_group_array", 1,
                           sqldeepJsonGroupArrayStep, sqldeepJsonGroupArrayFinal)
    return rc
}

// MARK: - Registration helpers

private func registerScalar(
    _ db: OpaquePointer, _ name: String, _ nArg: Int32,
    _ fn: @escaping @convention(c) (OpaquePointer?, Int32, UnsafeMutablePointer<OpaquePointer?>?) -> Void
) -> Int32 {
    sqlite3_create_function_v2(
        db, name, nArg, SQLITE_UTF8, nil,
        fn, nil, nil, nil)
}

private func registerAggregate(
    _ db: OpaquePointer, _ name: String, _ nArg: Int32,
    _ step: @escaping @convention(c) (OpaquePointer?, Int32, UnsafeMutablePointer<OpaquePointer?>?) -> Void,
    _ final: @escaping @convention(c) (OpaquePointer?) -> Void
) -> Int32 {
    sqlite3_create_function_v2(
        db, name, nArg, SQLITE_UTF8, nil,
        nil, step, final, nil)
}

// MARK: - Value helpers

private func isBlob(_ v: OpaquePointer?) -> Bool {
    sqlite3_value_type(v) == SQLITE_BLOB
}

private func valueText(_ v: OpaquePointer?) -> String {
    guard let p = sqlite3_value_text(v) else { return "" }
    return String(cString: p)
}

private func valueBlob(_ v: OpaquePointer?) -> Data {
    let n = sqlite3_value_bytes(v)
    guard n > 0, let p = sqlite3_value_blob(v) else { return Data() }
    return Data(bytes: p, count: Int(n))
}

private func resultBlob(_ ctx: OpaquePointer?, _ data: Data) {
    data.withUnsafeBytes { buf in
        sqlite3_result_blob(ctx, buf.baseAddress, Int32(buf.count), unsafeBitCast(-1, to: sqlite3_destructor_type.self))
    }
}

private func resultBlobString(_ ctx: OpaquePointer?, _ s: String) {
    resultBlob(ctx, Data(s.utf8))
}

// MARK: - Escaping

private func xmlEscapeText(_ s: String) -> String {
    var out = ""
    out.reserveCapacity(s.count)
    for c in s {
        switch c {
        case "<": out += "&lt;"
        case ">": out += "&gt;"
        case "&": out += "&amp;"
        default:  out.append(c)
        }
    }
    return out
}

private func xmlEscapeAttr(_ s: String) -> String {
    var out = ""
    out.reserveCapacity(s.count)
    for c in s {
        switch c {
        case "\"": out += "&quot;"
        case "<":  out += "&lt;"
        case ">":  out += "&gt;"
        case "&":  out += "&amp;"
        default:   out.append(c)
        }
    }
    return out
}

private func jsonEscape(_ s: String) -> String {
    var out = ""
    out.reserveCapacity(s.count)
    for c in s.unicodeScalars {
        switch c {
        case "\"":                out += "\\\""
        case "\\":                out += "\\\\"
        case "\u{08}":            out += "\\b"
        case "\u{0C}":            out += "\\f"
        case "\n":                out += "\\n"
        case "\r":                out += "\\r"
        case "\t":                out += "\\t"
        default:
            if c.value < 0x20 {
                out += String(format: "\\u%04x", c.value)
            } else {
                out += String(c)
            }
        }
    }
    return out
}

// MARK: - JSON value rendering (BLOB protocol)

/// Render a SQLite value as JSON text for embedding in JSON output.
private func jsonValueString(_ v: OpaquePointer?) -> String {
    let t = sqlite3_value_type(v)
    switch t {
    case SQLITE_NULL:
        return "null"
    case SQLITE_INTEGER, SQLITE_FLOAT:
        return valueText(v)
    case SQLITE_BLOB:
        let data = valueBlob(v)
        if data.first == UInt8(ascii: "<") {
            // XML BLOB — quote as JSON string
            return "\"" + jsonEscape(String(data: data, encoding: .utf8) ?? "") + "\""
        }
        // JSON BLOB — inline raw
        return String(data: data, encoding: .utf8) ?? ""
    default:
        // TEXT — quote as JSON string
        return "\"" + jsonEscape(valueText(v)) + "\""
    }
}

// MARK: - xml_attrs

private let xmlAttrs: @convention(c) (OpaquePointer?, Int32, UnsafeMutablePointer<OpaquePointer?>?) -> Void = { ctx, argc, argv in
    guard argc % 2 == 0 else {
        sqlite3_result_error(ctx, "xml_attrs requires even number of args", -1)
        return
    }
    var out = ""
    for i in stride(from: 0, to: Int(argc), by: 2) {
        let val = argv![i + 1]!
        guard sqlite3_value_type(val) != SQLITE_NULL else { continue }
        let name = valueText(argv![i]!)

        // Boolean BLOB: sqldeep_json('true'/'false')
        if isBlob(val) {
            let data = valueBlob(val)
            if data.count == 5, data.elementsEqual("false".utf8) { continue }
            out += " " + name
            continue
        }
        out += " " + name + "=\"" + xmlEscapeAttr(valueText(val)) + "\""
    }
    resultBlobString(ctx, out)
}

// MARK: - xml_element

private let xmlElement: @convention(c) (OpaquePointer?, Int32, UnsafeMutablePointer<OpaquePointer?>?) -> Void = { ctx, argc, argv in
    guard argc >= 1 else {
        sqlite3_result_error(ctx, "xml_element requires at least 1 arg", -1)
        return
    }
    var tag = valueText(argv![0]!)
    let selfClosing = tag.hasSuffix("/")
    if selfClosing { tag = String(tag.dropLast()) }

    var attrs = ""
    var childStart = 1
    if argc > 1, isBlob(argv![1]!) {
        let data = valueBlob(argv![1]!)
        if data.first == UInt8(ascii: " ") {
            attrs = String(data: data, encoding: .utf8) ?? ""
            childStart = 2
        }
    }

    var children = ""
    var hasChildren = false
    for i in childStart..<Int(argc) {
        let v = argv![i]!
        guard sqlite3_value_type(v) != SQLITE_NULL else { continue }
        hasChildren = true
        if isBlob(v) {
            let data = valueBlob(v)
            children += String(data: data, encoding: .utf8) ?? ""
        } else {
            children += xmlEscapeText(valueText(v))
        }
    }

    var out: String
    if selfClosing {
        out = "<" + tag + attrs + "/>"
    } else if hasChildren {
        out = "<" + tag + attrs + ">" + children + "</" + tag + ">"
    } else {
        out = "<" + tag + attrs + "></" + tag + ">"
    }
    resultBlobString(ctx, out)
}

// MARK: - xml_agg

private let xmlAggStep: @convention(c) (OpaquePointer?, Int32, UnsafeMutablePointer<OpaquePointer?>?) -> Void = { ctx, _, argv in
    let v = argv![0]!
    guard sqlite3_value_type(v) != SQLITE_NULL else { return }
    let pBuf = sqlite3_aggregate_context(ctx, Int32(MemoryLayout<AggBuf>.size))!
    let buf = pBuf.assumingMemoryBound(to: AggBuf.self)
    if buf.pointee.data == nil { buf.pointee.data = NSMutableData() }
    let md = buf.pointee.data!

    if isBlob(v) {
        let data = valueBlob(v)
        md.append(data)
    } else {
        let escaped = xmlEscapeText(valueText(v))
        md.append(Data(escaped.utf8))
    }
}

private let xmlAggFinal: @convention(c) (OpaquePointer?) -> Void = { ctx in
    let pBuf = sqlite3_aggregate_context(ctx, 0)
    guard let pBuf, let md = pBuf.assumingMemoryBound(to: AggBuf.self).pointee.data else {
        sqlite3_result_blob(ctx, "", 0, nil)
        return
    }
    resultBlob(ctx, md as Data)
}

// MARK: - xml_attrs_jsonml

private let xmlAttrsJsonml: @convention(c) (OpaquePointer?, Int32, UnsafeMutablePointer<OpaquePointer?>?) -> Void = { ctx, argc, argv in
    guard argc % 2 == 0 else {
        sqlite3_result_error(ctx, "xml_attrs_jsonml requires even number of args", -1)
        return
    }
    var parts: [String] = []
    for i in stride(from: 0, to: Int(argc), by: 2) {
        let val = argv![i + 1]!
        guard sqlite3_value_type(val) != SQLITE_NULL else { continue }
        let name = jsonEscape(valueText(argv![i]!))
        let v = jsonEscape(valueText(val))
        parts.append("\"" + name + "\":\"" + v + "\"")
    }
    resultBlobString(ctx, "{" + parts.joined(separator: ",") + "}")
}

// MARK: - xml_element_jsonml (also used for jsx)

private let xmlElementJsonml: @convention(c) (OpaquePointer?, Int32, UnsafeMutablePointer<OpaquePointer?>?) -> Void = { ctx, argc, argv in
    guard argc >= 1 else {
        sqlite3_result_error(ctx, "xml_element_jsonml requires at least 1 arg", -1)
        return
    }
    var tag = valueText(argv![0]!)
    if tag.hasSuffix("/") { tag = String(tag.dropLast()) }

    var childStart = 1
    var attrsStr: String?

    // Detect attrs BLOB: starts with '{'
    if argc > 1, isBlob(argv![1]!) {
        let data = valueBlob(argv![1]!)
        if data.first == UInt8(ascii: "{") {
            attrsStr = String(data: data, encoding: .utf8)
            childStart = 2
        }
    }

    var out = "[\"" + jsonEscape(tag) + "\""
    if let a = attrsStr { out += "," + a }

    for i in childStart..<Int(argc) {
        let v = argv![i]!
        guard sqlite3_value_type(v) != SQLITE_NULL else { continue }
        if isBlob(v) {
            let data = valueBlob(v)
            guard !data.isEmpty else { continue }
            out += "," + (String(data: data, encoding: .utf8) ?? "")
        } else {
            out += ",\"" + jsonEscape(valueText(v)) + "\""
        }
    }
    out += "]"
    resultBlobString(ctx, out)
}

// MARK: - jsonml_agg / jsx_agg

private let jsonmlAggStep: @convention(c) (OpaquePointer?, Int32, UnsafeMutablePointer<OpaquePointer?>?) -> Void = { ctx, _, argv in
    let v = argv![0]!
    guard sqlite3_value_type(v) != SQLITE_NULL else { return }
    let pBuf = sqlite3_aggregate_context(ctx, Int32(MemoryLayout<AggBuf>.size))!
    let buf = pBuf.assumingMemoryBound(to: AggBuf.self)
    if buf.pointee.data == nil { buf.pointee.data = NSMutableData() }
    let md = buf.pointee.data!

    if md.length > 0 { md.append(Data(",".utf8)) }
    if isBlob(v) {
        md.append(valueBlob(v))
    } else {
        let s = "\"" + jsonEscape(valueText(v)) + "\""
        md.append(Data(s.utf8))
    }
}

private let jsonmlAggFinal: @convention(c) (OpaquePointer?) -> Void = { ctx in
    let pBuf = sqlite3_aggregate_context(ctx, 0)
    guard let pBuf, let md = pBuf.assumingMemoryBound(to: AggBuf.self).pointee.data else {
        sqlite3_result_blob(ctx, "", 0, nil)
        return
    }
    resultBlob(ctx, md as Data)
}

// MARK: - xml_attrs_jsx

private let xmlAttrsJsx: @convention(c) (OpaquePointer?, Int32, UnsafeMutablePointer<OpaquePointer?>?) -> Void = { ctx, argc, argv in
    guard argc % 2 == 0 else {
        sqlite3_result_error(ctx, "xml_attrs_jsx requires even number of args", -1)
        return
    }
    var parts: [String] = []
    for i in stride(from: 0, to: Int(argc), by: 2) {
        let val = argv![i + 1]!
        guard sqlite3_value_type(val) != SQLITE_NULL else { continue }
        let name = "\"" + jsonEscape(valueText(argv![i]!)) + "\""

        // Check raw BEFORE sqlite3_value_text coerces BLOB→TEXT
        if jsxIsRaw(val) {
            let raw: String
            if sqlite3_value_type(val) == SQLITE_BLOB {
                raw = String(data: valueBlob(val), encoding: .utf8) ?? ""
            } else {
                raw = valueText(val)
            }
            parts.append(name + ":" + raw)
        } else {
            parts.append(name + ":\"" + jsonEscape(valueText(val)) + "\"")
        }
    }
    resultBlobString(ctx, "{" + parts.joined(separator: ",") + "}")
}

private func jsxIsRaw(_ v: OpaquePointer?) -> Bool {
    let t = sqlite3_value_type(v)
    if t == SQLITE_INTEGER || t == SQLITE_FLOAT { return true }
    if t == SQLITE_BLOB {
        let data = valueBlob(v)
        return !data.isEmpty && data[0] != UInt8(ascii: "<")
    }
    return false
}

// MARK: - sqldeep_json

private let sqldeepJson: @convention(c) (OpaquePointer?, Int32, UnsafeMutablePointer<OpaquePointer?>?) -> Void = { ctx, _, argv in
    let v = argv![0]!
    guard sqlite3_value_type(v) != SQLITE_NULL else {
        sqlite3_result_null(ctx)
        return
    }
    resultBlobString(ctx, valueText(v))
}

// MARK: - sqldeep_json_object

private let sqldeepJsonObject: @convention(c) (OpaquePointer?, Int32, UnsafeMutablePointer<OpaquePointer?>?) -> Void = { ctx, argc, argv in
    guard argc % 2 == 0 else {
        sqlite3_result_error(ctx, "sqldeep_json_object requires even number of args", -1)
        return
    }
    var parts: [String] = []
    for i in stride(from: 0, to: Int(argc), by: 2) {
        let key = "\"" + jsonEscape(valueText(argv![i]!)) + "\""
        let val = jsonValueString(argv![i + 1]!)
        parts.append(key + ":" + val)
    }
    resultBlobString(ctx, "{" + parts.joined(separator: ",") + "}")
}

// MARK: - sqldeep_json_array

private let sqldeepJsonArray: @convention(c) (OpaquePointer?, Int32, UnsafeMutablePointer<OpaquePointer?>?) -> Void = { ctx, argc, argv in
    var parts: [String] = []
    for i in 0..<Int(argc) {
        parts.append(jsonValueString(argv![i]!))
    }
    resultBlobString(ctx, "[" + parts.joined(separator: ",") + "]")
}

// MARK: - sqldeep_json_group_array

private let sqldeepJsonGroupArrayStep: @convention(c) (OpaquePointer?, Int32, UnsafeMutablePointer<OpaquePointer?>?) -> Void = { ctx, _, argv in
    let v = argv![0]!
    guard sqlite3_value_type(v) != SQLITE_NULL else { return }
    let pBuf = sqlite3_aggregate_context(ctx, Int32(MemoryLayout<AggBuf>.size))!
    let buf = pBuf.assumingMemoryBound(to: AggBuf.self)
    if buf.pointee.data == nil { buf.pointee.data = NSMutableData() }
    let md = buf.pointee.data!

    if md.length > 0 { md.append(Data(",".utf8)) }
    md.append(Data(jsonValueString(v).utf8))
}

private let sqldeepJsonGroupArrayFinal: @convention(c) (OpaquePointer?) -> Void = { ctx in
    let pBuf = sqlite3_aggregate_context(ctx, 0)
    guard let pBuf, let md = pBuf.assumingMemoryBound(to: AggBuf.self).pointee.data else {
        resultBlobString(ctx, "[]")
        return
    }
    var out = Data("[".utf8)
    out.append(md as Data)
    out.append(Data("]".utf8))
    resultBlob(ctx, out)
}

// MARK: - Aggregate context

/// Aggregate buffer for accumulating bytes across rows.
/// Uses NSMutableData for pointer stability across sqlite3_aggregate_context calls.
private struct AggBuf {
    var data: NSMutableData?
}
