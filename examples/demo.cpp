// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0
#include "sqldeep.h"
#include <iostream>

int main() {
    std::string input = R"(SELECT {
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

    try {
        std::string output = sqldeep::transpile(input);
        std::cout << "Output:\n" << output << "\n";
    } catch (const sqldeep::Error& e) {
        std::cerr << "Error at line " << e.line() << ", col " << e.col()
                  << ": " << e.what() << "\n";
        return 1;
    }

    return 0;
}
