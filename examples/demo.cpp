// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0
#include "sqldeep.h"
#include <iostream>

int main() {
    const char* input = R"(SELECT {
    id,
    name,
    orders:
        SELECT {
            order_id: id,
            items:
                SELECT {
                    item: 'item-' || name,
                    qty,
                } FROM items i WHERE order_id = o.id,
        } FROM orders o WHERE customer_id = p.id,
} FROM people p;)";

    std::cout << "Input:\n" << input << "\n\n";

    char* err_msg = nullptr;
    int err_line = 0, err_col = 0;
    char* output = sqldeep_transpile(input, &err_msg, &err_line, &err_col);

    if (output) {
        std::cout << "Output:\n" << output << "\n";
        sqldeep_free(output);
        return 0;
    } else {
        std::cerr << "Error at line " << err_line << ", col " << err_col
                  << ": " << (err_msg ? err_msg : "unknown") << "\n";
        sqldeep_free(err_msg);
        return 1;
    }
}
