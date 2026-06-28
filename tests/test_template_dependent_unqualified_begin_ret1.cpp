template <class T>
auto begin(T& value) -> decltype(value.not_begin()) {
	return value.not_begin();
}

template <class T>
auto dependentBeginProbe(T& value) -> decltype(begin(value)) {
	return begin(value);
}

namespace DependentBeginAdl {
	struct Range {
		int not_begin() {
			return data;
		}

		int data;
	};
}

int main() {
	DependentBeginAdl::Range range{1};
	return dependentBeginProbe(range);
}
