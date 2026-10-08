// [except.spec]/7: a static member function's noexcept operand must be a
// constant expression. The rejection must be a located diagnostic carrying
// NoexceptSpecifierNotConstant (#1021), and the specifier must not be dropped.
int runtime_value();

struct Probe {
	static void run() noexcept(runtime_value());
};

int main() {
	return 0;
}
