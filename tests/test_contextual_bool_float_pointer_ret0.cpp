// Regression test: float* and double* in boolean contexts must compare the
// pointer address (not the pointed-to float value) against null.
// Bug: applyConditionBoolConversion reads only base_type from the sema
// annotation's source_type_id, ignoring pointer_levels.  For float* the
// base_type is Float, so is_floating_point_type() returns true and the
// codegen emits FloatNotEqual (a floating-point comparison) on a 64-bit
// pointer address, producing incorrect results.

int main() {
	float f = 0.0f;
	float* fp = &f;       // non-null pointer to a zero float
	float* fn = nullptr;  // null pointer

	double d = 0.0;
	double* dp = &d;      // non-null pointer to a zero double
	double* dn = nullptr; // null pointer

	int result = 0;

	// Non-null float pointer: truthy (even though *fp == 0.0f).
	if (fp) {
		result += 1;
	}

	// Null float pointer: falsy.
	if (fn) {
		result += 100;  // should NOT execute
	}

	// Non-null double pointer: truthy (even though *dp == 0.0).
	if (dp) {
		result += 10;
	}

	// Null double pointer: falsy.
	if (dn) {
		result += 100;  // should NOT execute
	}

	// Expected: result == 11 (1 + 10)
	return result - 11;
}
