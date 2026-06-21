struct NonTemplateCallable {
	static int operator()(int x) {
		return x + 1;
	}
};

template <typename T>
struct TemplateCallable {
	static int operator()(T x) {
		return static_cast<int>(x) + 2;
	}
};

int main() {
	int object_non_template = NonTemplateCallable{}(1);
	int object_template = TemplateCallable<int>{}(2);
	return object_non_template + object_template - 6;
}
