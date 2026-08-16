// Reduced structural-coverage regression for expression child surfaces.
// The call below exercises a receiver, explicit template arguments, and
// ordinary call arguments in one materialized template body.  The allocation
// exercises a new-expression's allocated type and optional array bound.

template <typename... OwnerArgs>
struct Accumulator {
	// Keep the owner template parameter pack separate from the call's argument
	// pack so the receiver itself is dependent during substitution.
	int sum(int first, int second, int third) {
		return first + second + third;
	}
};

namespace helper {
template <typename... Ts>
int count_types() {
	return static_cast<int>(sizeof...(Ts));
}
}

template <typename... Ts>
int invoke(Ts... values) {
	Accumulator<Ts...> accumulator;
	int* storage = new int[3];
	storage[0] = 1;
	int result = accumulator.sum(values...) + helper::count_types<Ts...>() + storage[0] - 4;
	delete[] storage;
	return result;
}

int main() {
	return invoke(10, 15, 17);
}
