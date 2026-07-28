// Regression: local auto& deduction in a class-template body must stay
// dependent until nested member types have been substituted.
template <typename T>
struct Storage {
	T* first;
	T* last;
};

template <typename First, typename Second, bool = true>
struct CompressedPair {
	First first;
	Second second;
};

template <typename T>
class Container {
	using StorageType = Storage<T>;

	CompressedPair<int, StorageType> data;

public:
	int test() {
		auto& pair = data;
		auto& storage = pair.second;
		auto& last = storage.last;
		auto& direct = data.second.first;
		T*& typed_last = last;
		T*& typed_direct = direct;
		(void) typed_last;
		(void) typed_direct;
		return 21;
	}
};

int main() {
	// These calls force both bodies to instantiate and type-check. Keep them out
	// of the executed path because reference lowering is a separate codegen concern.
	if (false) {
		Container<int> int_container;
		Container<long long> wide_container;
		return int_container.test() + wide_container.test();
	}
	return 42;
}
