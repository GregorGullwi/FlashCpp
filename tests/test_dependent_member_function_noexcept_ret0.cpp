// A class-template member function's dependent noexcept operand must be
// re-evaluated for each specialization. The instantiated member's answer is
// observable through an unqualified in-member call. Before this was fixed the
// instantiation copied the pattern's interim answer, so a specialization whose
// operand folds to false still reported the member as noexcept.
struct Companion {
	int value = 0;
};

template <bool Enabled>
struct Probe {
	int tag = 0;
	Companion companion;

	void f() noexcept(!Enabled) {}

	void check() {
		static_assert(noexcept(f()) == !Enabled, "f is noexcept exactly when not Enabled");
	}
};

int main() {
	Probe<true> enabled;
	Probe<false> disabled;
	enabled.check();
	disabled.check();
	return enabled.tag == 0 && disabled.tag == 0 && disabled.companion.value == 0 ? 0 : 1;
}
