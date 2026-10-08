// A class-template destructor's dependent noexcept operand must be re-evaluated
// for each specialization. Before this was fixed the instantiated destructor
// kept the pattern's answer, so __is_nothrow_destructible was wrong for the
// specialization whose operand folds to false.
struct Companion {
	int value = 0;
};

template <bool Enabled>
struct Probe {
	int tag = 0;
	Companion companion;
	~Probe() noexcept(Enabled) {}
};

static_assert(__is_nothrow_destructible(Probe<true>), "enabled destructor is noexcept");
static_assert(!__is_nothrow_destructible(Probe<false>), "disabled destructor can throw");

// The pseudo-destructor spelling resolves the instantiated destructor too.
static_assert(noexcept(((Probe<true>*)nullptr)->~Probe()), "enabled pseudo-destructor is noexcept");
static_assert(!noexcept(((Probe<false>*)nullptr)->~Probe()), "disabled pseudo-destructor can throw");
static_assert(noexcept((*((Probe<true>*)nullptr)).~Probe()), "enabled dereferenced pseudo-destructor is noexcept");
static_assert(!noexcept((*((Probe<false>*)nullptr)).~Probe()), "disabled dereferenced pseudo-destructor can throw");

int main() {
	Probe<true> enabled;
	Probe<false> disabled;
	return enabled.tag == 0 && disabled.tag == 0 && disabled.companion.value == 0 ? 0 : 1;
}
