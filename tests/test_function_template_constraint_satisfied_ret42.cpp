// A satisfied constrained function template must still instantiate. The
// missing-candidate fallback fires only when no specialization is viable, so
// this guards it against reporting NoViableFunctionCall for a good call.

template <typename T>
concept IsInt = __is_same(T, int);

template <typename T>
	requires IsInt<T>
int identity(T value) {
	return value;
}

int main() {
	return identity(42);
}
