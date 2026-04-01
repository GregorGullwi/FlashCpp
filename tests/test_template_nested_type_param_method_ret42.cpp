// Hardening test: nested type with type parameter in default initializer,
// returned from member function.  Exercises both sizeof(T) resolution and
// nested type remapping for different template arguments.
template <typename T>
struct Container {
	struct Entry {
		int size_val = static_cast<int>(sizeof(T));
		int doubled = static_cast<int>(sizeof(T)) * 2;
	};

	Entry makeEntry() {
		return Entry{};
	}
};

int main() {
	Container<int> ci;
	Container<int>::Entry ei = ci.makeEntry();
	Container<double> cd;
	Container<double>::Entry ed = cd.makeEntry();

	if (ei.size_val != 4) return 1;
	if (ei.doubled != 8) return 2;
	if (ed.size_val != 8) return 3;
	if (ed.doubled != 16) return 4;

	return ei.size_val + ei.doubled + ed.size_val + ed.doubled + 6;  // 4+8+8+16+6 = 42
}
