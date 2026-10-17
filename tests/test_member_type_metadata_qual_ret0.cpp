// Regression: published member types must preserve base and intermediate pointer CV.
int select_pointee(const int*) {
	return 10;
}

int select_pointee(int*) {
	return 20;
}

int select_intermediate(int* const*) {
	return 30;
}

int select_intermediate(int**) {
	return 40;
}

struct View {
	const int* data;
	int* const* chain;
	const int values[2];
};

template <typename T>
struct Box {
	T value;
};

template <typename T>
struct Box<T*> {
	const T* value;
};

template <>
struct Box<long> {
	const int* value;
};

struct RefView {
	const int*& data;
};

struct DerivedView : View {};

int main() {
	int value = 0;
	int* pointer = &value;
	int* const* chain = &pointer;
	View view{&value, chain, {1, 2}};
	DerivedView derived{{&value, chain, {1, 2}}};
	const int* const_pointer = &value;
	RefView reference{const_pointer};
	Box<int*> partial{&value};
	Box<long> full{&value};
	return select_pointee(view.data) == 10 && select_intermediate(view.chain) == 30 &&
		select_pointee(view.values) == 10 && select_pointee(derived.data) == 10 &&
		select_pointee(reference.data) == 10 && select_pointee(partial.value) == 10 &&
		select_pointee(full.value) == 10 ? 0 : 1;
}
