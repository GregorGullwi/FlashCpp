int base_conversion_calls = 0;
int derived_conversion_calls = 0;

struct Base {
	operator int*() { ++base_conversion_calls; return nullptr; }
};

struct Derived : Base {
	operator int**() { ++derived_conversion_calls; return nullptr; }
};

int main() {
	Derived object{};
	int* base_conversion = object;
	int** derived_conversion = object;
	return base_conversion_calls == 1 && derived_conversion_calls == 1 ? 42 : 1;
}
