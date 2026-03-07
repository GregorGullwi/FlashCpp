// Test: _Pragma("once") should behave like #pragma once for the current header.
#include "test_pragma_once_operator_header.h"
#include "test_pragma_once_operator_header.h"

int main() {
	return pragma_once_operator_value;
}
