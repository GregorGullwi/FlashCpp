using ThrowingFunction = int (*)(int);
using NoexceptFunction = int (*)(int) noexcept;

int choose(ThrowingFunction) {
	return 1;
}

int choose(NoexceptFunction) {
	return 2;
}

int plainTarget(int value) {
	return value;
}

int noexceptTarget(int value) noexcept {
	return value;
}

int main() {
	NoexceptFunction noexcept_pointer = &noexceptTarget;
	ThrowingFunction throwing_pointer = &plainTarget;
	const NoexceptFunction const_noexcept_pointer = noexcept_pointer;
	if (choose(noexcept_pointer) != 2)
		return 1;
	if (choose(throwing_pointer) != 1)
		return 2;
	if (choose(const_noexcept_pointer) != 2)
		return 3;
	return 0;
}
