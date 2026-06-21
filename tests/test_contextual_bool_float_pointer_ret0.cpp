// Correctness test: float* and double* in boolean contexts must compare the
// pointer address (not the pointed-to float value) against null.
// Covers a latent codegen issue where applyConditionBoolConversion reads only
// base_type from the sema annotation's source_type_id, ignoring pointer_levels.
// For float* the base_type is Float, so is_floating_point_type() returns true
// and codegen emits FloatNotEqual (a floating-point comparison) on a 64-bit
// pointer address.  In practice this produces correct results because no valid
// pointer address has the bit pattern of ±0.0 double, but the emitted
// instruction is semantically wrong (should be integer TEST, not UCOMISD).

int main() {
	float f = 0.0f;
	float* fp = &f;		// non-null pointer to a zero float
	float* fn = nullptr; // null pointer

	double d = 0.0;
	double* dp = &d;	 // non-null pointer to a zero double
	double* dn = nullptr; // null pointer

	// Non-null float pointer: truthy (even though *fp == 0.0f).
	if (!fp) {
		return 1;
	}

	// Null float pointer: falsy.
	if (fn) {
		return 2;
	}

	// Non-null double pointer: truthy (even though *dp == 0.0).
	if (!dp) {
		return 3;
	}

	// Null double pointer: falsy.
	if (dn) {
		return 4;
	}

	// Logical operators and ternary must observe the normalized bool result of
	// contextual conversion, not reinterpret the raw pointer bits as bool8.
	if (!(fp && dp)) {
		return 5;
	}

	if (fp && fn) {
		return 6;
	}

	if (!(fp || fn)) {
		return 7;
	}

	if (fn || dn) {
		return 8;
	}

	if (!!fn) {
		return 9;
	}

	if (!fp) {
		return 10;
	}

	if ((fp ? 1 : 0) != 1) {
		return 11;
	}

	if ((fn ? 1 : 0) != 0) {
		return 12;
	}

	if ((dn ? 1 : 0) != 0) {
		return 13;
	}

	if ((dp ? 1 : 0) != 1) {
		return 14;
	}

	return 0;
}
