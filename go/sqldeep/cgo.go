// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0

package sqldeep

//#cgo CFLAGS: -I${SRCDIR}/../../dist -I${SRCDIR}/../../vendor/include
//#cgo LDFLAGS: ${SRCDIR}/../../build/libsqldeep.a ${SRCDIR}/../../build/sqldeep_xml.o ${SRCDIR}/../../build/sqlite3.o -lstdc++ -lz
//#include "sqldeep.h"
//#include <stdlib.h>
import "C"
import "unsafe"

// goError converts C error out-params to a Go *Error. Frees the C message.
// Returns nil if errMsg is nil (no error).
func goError(errMsg *C.char, errLine, errCol C.int) error {
	if errMsg == nil {
		return nil
	}
	msg := C.GoString(errMsg)
	C.sqldeep_free(unsafe.Pointer(errMsg))
	return &Error{Msg: msg, Line: int(errLine), Col: int(errCol)}
}
