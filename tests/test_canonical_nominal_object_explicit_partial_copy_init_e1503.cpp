struct Input { short value; };
template<class T> struct Target;
template<class T> struct Target<T*> {
	explicit Target(const Input& input) : value(input.value) {}
	T value;
};

int main() {
	Input input{9};
	Target<short*> target = input;
	return 0;
}
