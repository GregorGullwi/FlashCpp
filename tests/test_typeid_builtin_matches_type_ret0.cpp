// Regression test: typeid on built-in/arithmetic types must produce stable,
// distinct, and comparable type_info identities.
//
// On ELF/Itanium, typeid(int) resolves to the external ::std::type_info
// _ZTIi from libstdc++. On Windows/COFF, typeid(int) must now resolve to a
// real ??_R0H@8 Type Descriptor in .rdata (previously a per-TU hash
// placeholder). Both platforms are expected to satisfy the same identity
// relations exercised below.

int main() {
	int i = 7;
	float f = 2.0f;
	double d = 3.5;
	bool b = true;

	const void* int_ty    = typeid(int);
	const void* uint_ty   = typeid(unsigned int);
	const void* float_ty  = typeid(float);
	const void* double_ty = typeid(double);
	const void* bool_ty   = typeid(bool);
	const void* void_ty   = typeid(void);

	if (!int_ty || !uint_ty || !float_ty || !double_ty || !bool_ty || !void_ty) {
		return 1;
	}

	// typeid(expr) on non-polymorphic built-ins is a compile-time constant and
	// must match typeid(T) for the same T.
	if (typeid(i) != int_ty)    return 2;
	if (typeid(f) != float_ty)  return 3;
	if (typeid(d) != double_ty) return 4;
	if (typeid(b) != bool_ty)   return 5;

	// Distinct built-in types must produce distinct type_info pointers.
	if (int_ty == uint_ty)      return 6;
	if (int_ty == float_ty)     return 7;
	if (float_ty == double_ty)  return 8;
	if (int_ty == bool_ty)      return 9;
	if (int_ty == void_ty)      return 10;

	return 0;
}
