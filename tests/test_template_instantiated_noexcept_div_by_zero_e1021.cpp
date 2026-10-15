// A dependent noexcept specification that becomes a division by zero after
// substitution is not a constant expression and must report
// NoexceptSpecifierNotConstant.

template <typename T>
void guarded() noexcept(1 / (sizeof(T) - 4)) {}

int main() {
	guarded<int>();
	return 0;
}
