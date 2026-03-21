// Test consteval member function called on a non-constexpr object.
// C++20 [dcl.consteval]: every call to a consteval function must be a constant
// expression — but the *object* need not be constexpr when the member function
// does not read any member state through 'this'.
//
// This is valid C++20 and accepted by GCC/Clang/MSVC.  FlashCpp currently
// rejects it because the evaluator requires the object to be constexpr.

struct Calc {
	consteval int triple(int v) const { return v * 3; }
};

int main() {
	Calc c;                  // non-constexpr local
	return c.triple(14) - 42; // 14*3 - 42 == 0
}
