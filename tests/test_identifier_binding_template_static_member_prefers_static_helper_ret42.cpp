// Phase 3C regression: template static member initializers should prefer
// class static helper functions over same-named globals.

constexpr int helper() {
	return 7;
}

template <typename T>
struct Box {
	static constexpr int helper() {
		return static_cast<int>(sizeof(T)) + 38;
	}

	static constexpr int value = helper();
};

int main() {
	return Box<int>::value;
}
