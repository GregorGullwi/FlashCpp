// Regression: injected-class return overloads on a partial specialization must
// still rank receiver cv through replay (const receiver → const overload).
template <class T>
struct Wrap;

template <class T>
struct Wrap<T*> {
	int tag = 0;

	Wrap& self() { tag = 2; return *this; }
	const Wrap& self() const { return *this; }

	int read() const { return self().tag == 0 ? 1 : 0; }
};

int main() {
	const Wrap<int*> w{};
	return w.read() == 1 ? 42 : 0;
}
