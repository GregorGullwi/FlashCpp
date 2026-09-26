template <typename T>
struct StaticArrayPointer {
	inline static T (*value)[3] = nullptr;
};

int accept(int (*value)[3]) {
	(void)value;
	return 1;
}

int accept(double (*value)[3]) {
	(void)value;
	return 2;
}

int main() {
	StaticArrayPointer<int> ints;
	StaticArrayPointer<double> doubles;
	return accept(ints.value) == 1 && accept(doubles.value) == 2
		? 42
		: 0;
}
