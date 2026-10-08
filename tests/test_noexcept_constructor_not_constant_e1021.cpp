// [except.spec]/7: a constructor's noexcept operand must be a constant
// expression. The rejection must be a located diagnostic carrying
// NoexceptSpecifierNotConstant (#1021), not an unlocated internal failure.
int runtime_value();

struct Probe {
	Probe() noexcept(runtime_value()) {}
};

int main() {
	Probe probe{};
	return 0;
}
