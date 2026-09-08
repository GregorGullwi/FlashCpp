// Type-only template specializations share primary TemplateDeclId identity while
// argument TypeIds distinguish Pair<double,float> from Pair<float,double>.
template <typename T, typename U>
struct Pair {
	T first;
	U second;
};

template <typename T>
struct Box {
	T value;
};

template <typename T>
T identity(T value) {
	return value;
}

int main() {
	Pair<double, float> wide{3.5, 1.5f};
	Pair<float, double> swapped{2.5f, 4.5};
	Box<short> narrow{9};
	char tiny = 1;
	return static_cast<int>(identity(wide.first)) +
		static_cast<int>(identity(wide.second)) +
		static_cast<int>(identity(swapped.first)) +
		static_cast<int>(identity(swapped.second)) +
		static_cast<int>(identity(narrow.value)) +
		static_cast<int>(identity(tiny)) -
		(3 + 1 + 2 + 4 + 9 + 1);
}
