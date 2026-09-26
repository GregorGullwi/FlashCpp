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
	return
		accept(StaticArrayPointer<int>::value) == 1 &&
		accept(StaticArrayPointer<double>::value) == 2
		? 42
		: 0;
}
