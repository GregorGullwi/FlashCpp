// [except.spec]/7: a function's noexcept operand must be a constant expression.
// The rejection must be a located diagnostic carrying
// NoexceptSpecifierNotConstant (#1021).
int runtime_value();

void probe() noexcept(runtime_value());

int main() {
	return 0;
}
