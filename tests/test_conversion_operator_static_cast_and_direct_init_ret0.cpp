// static_cast and direct-initialization of a class object invoke a conversion
// operator, including an explicit one ([expr.static.cast]/4, [dcl.init]). These
// previously reinterpreted the object representation and produced a garbage
// value instead of calling the operator. Return 0 only when every form yields
// the converted value, including when the cast feeds a comparison (which the
// constant evaluator must defer to lowering).
struct ExplicitToInt {
	explicit operator int() const { return 7; }
};

struct ImplicitToLong {
	operator long() const { return 9; }
};

int main() {
	int r = 0;
	r |= static_cast<int>(ExplicitToInt{}) == 7 ? 0 : 1;
	r |= static_cast<long>(ImplicitToLong{}) == 9 ? 0 : 2;
	r |= static_cast<int>(ExplicitToInt{}) + static_cast<int>(ExplicitToInt{}) == 14 ? 0 : 4;
	r |= static_cast<double>(static_cast<int>(ExplicitToInt{})) == 7.0 ? 0 : 8;

	int parenthesized(ExplicitToInt{});
	r |= parenthesized == 7 ? 0 : 16;

	int braced{ImplicitToLong{}};
	r |= braced == 9 ? 0 : 32;

	return r;
}
