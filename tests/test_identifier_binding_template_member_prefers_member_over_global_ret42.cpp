// Phase 3B regression: instantiated template member bodies should prefer
// implicit members over same-named globals.

int value = 5;

template <typename T>
struct Box {
	int value;

	template <typename U>
	int get(U extra) {
		return value + static_cast<int>(extra);
	}
};

int main() {
	Box<int> box{40};
	return box.get<int>(2);
}
