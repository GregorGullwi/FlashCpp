// Regression: calling one member must not enqueue unrelated bodyless members.
// An undefined member is valid as long as it is not ODR-used.
template <typename T>
struct Holder {
	T value;

	void increment() {
		value += 1;
	}

	Holder& operator=(const Holder&);
};

struct Caller : Holder<int> {
	int run() {
		Holder<int>::increment();
		return value;
	}
};

int main() {
	Caller caller;
	caller.value = 41;
	return caller.run();
}
