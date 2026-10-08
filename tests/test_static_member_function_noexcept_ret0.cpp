// A static member function's noexcept specifier must be recorded, not dropped.
// Before this was fixed the static-member parse path skipped the trailing
// specifier, so even `static void f() noexcept(true)` was reported throwing.
struct Probe {
	static void nothrowing() noexcept(true) {}
	static void throwing() noexcept(false) {}
	int tag = 0;

	void check() {
		static_assert(noexcept(nothrowing()), "static noexcept(true) is noexcept");
		static_assert(!noexcept(throwing()), "static noexcept(false) can throw");
	}
};

int main() {
	Probe probe{};
	probe.check();
	return probe.tag == 0 ? 0 : 1;
}
