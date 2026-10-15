// A dependent noexcept specification that, after instantiation, calls a
// non-constexpr function is not a constant expression. The instantiated fold
// must report NoexceptSpecifierNotConstant rather than an unlocated string.

int callee(int);

template <typename T>
void guarded() noexcept(callee(T{})) {}

int main() {
	guarded<int>();
	return 0;
}
