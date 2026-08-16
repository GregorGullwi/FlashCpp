// Reduced non-std regression for the structural expression rewriter.
// Each function exercises a pack-expanded argument sequence in a different
// owning expression: call, constructor, placement new, and new-expression.

struct Pair {
	int first;
	int second;

	Pair(int left, int right) : first(left), second(right) {}
};

int sum_pair(int left, int right) {
	return left + right;
}

void* operator new(unsigned long long, void* address) noexcept {
	return address;
}

template <typename... Args>
int call_with_pack(Args... args) {
	return sum_pair(args...);
}

template <typename... Args>
Pair construct_with_pack(Args... args) {
	return Pair(args...);
}

template <typename... Args>
int placement_with_pack(Pair* address, Args... args) {
	new (address) Pair(args...);
	return address->first + address->second;
}

template <typename... Args>
int new_with_pack(Args... args) {
	Pair* value = new Pair(args...);
	int result = value->first + value->second;
	delete value;
	return result;
}

int main() {
	Pair constructed = construct_with_pack(20, 22);
	Pair placed(0, 0);
	int call_result = call_with_pack(20, 22);
	int placement_result = placement_with_pack(&placed, 20, 22);
	int new_result = new_with_pack(20, 22);
	return call_result == 42 &&
			constructed.first + constructed.second == 42 &&
			placement_result == 42 &&
			new_result == 42 &&
			placed.first + placed.second == 42
		? 42
		: 0;
}
