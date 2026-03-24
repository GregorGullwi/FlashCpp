// const_cast<int&&>(lvalue) changes the reference kind from lvalue to rvalue,
// which is not a valid const_cast — const_cast may only change cv-qualification
// per C++20 [expr.const.cast].  The evaluator should reject this.
constexpr int value = 42;

constexpr int bad_const_cast_rvalue_ref() {
	return const_cast<int&&>(value);
}

static_assert(bad_const_cast_rvalue_ref() == 42);

int main() {
	return 0;
}
