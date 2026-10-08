// A member call on a class-template specialization keeps the pattern member as
// its callee, so the constexpr noexcept evaluator must resolve the instantiated
// member through the receiver's type. Before this was fixed an external
// static_assert observed the pattern's answer for every specialization.
template <bool Enabled>
struct Probe {
	void f() noexcept(Enabled) {}
	int tag = 0;
};

static_assert(noexcept(((Probe<true>*)nullptr)->f()), "enabled specialization is noexcept");
static_assert(!noexcept(((Probe<false>*)nullptr)->f()), "disabled specialization can throw");

int main() {
	Probe<true> enabled;
	Probe<false> disabled;
	return enabled.tag == 0 && disabled.tag == 0 ? 0 : 1;
}
