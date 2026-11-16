// A dependent explicit(condition) operand must be evaluated for the deduced
// conversion target, not for the template pattern.

struct ConditionalExplicitConversion {
	template<class U>
	explicit(sizeof(U) > sizeof(int)) operator U() const {
		return U{ sizeof(U) == sizeof(int) ? 1 : 2 };
	}
};

int main() {
	int implicit_int = ConditionalExplicitConversion{};
	if (implicit_int != 1) {
		return 1;
	}
	long long explicit_long_long = static_cast<long long>(ConditionalExplicitConversion{});
	if (explicit_long_long != 2) {
		return 2;
	}
	return 0;
}
