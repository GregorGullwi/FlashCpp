// Regression: partial-specialization member-body replay must rank receiver
// cv-qualification when selecting among otherwise-identical const/non-const
// overloads. Declaration order must not decide the winner.
template <class T>
struct Holder;

template <class T>
struct Holder<T*> {
	// Non-const declared first so naive "first match" wrongly wins for a const
	// receiver unless replay ranks the implicit object conversion.
	int get() { return 2; }
	int get() const { return 1; }
	int call() const { return get(); }
	int call_mut() { return get(); }
};

int main() {
	const Holder<int*> ch{};
	Holder<int*> h{};
	if (ch.call() != 1) {
		return 1;
	}
	if (h.call_mut() != 2) {
		return 2;
	}
	return 42;
}
