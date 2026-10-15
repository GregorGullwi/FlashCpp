// A decltype(auto) local whose initializer type is inferable must still deduce;
// the structured rejection must not fire for a resolvable initializer.

template <typename T>
int deduce() {
	decltype(auto) value = sizeof(T);
	return static_cast<int>(value);
}

int main() {
	return deduce<int>() == 4 ? 0 : 1;
}
