// A const T&& parameter is not a forwarding reference and cannot bind an lvalue,
// including when a trailing function parameter pack can consume later arguments.

template <class T, class... Rest>
constexpr int acceptConstRvalue(const T&&, Rest&&...) {
	return sizeof...(Rest);
}

int main() {
	int value = 1;
	return acceptConstRvalue(value, 2);
}
