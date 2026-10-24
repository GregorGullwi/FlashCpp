struct Input { short value; };
template<class T> struct Target {
	explicit Target(const Input& input) : value(input.value) {}
	T value;
};

int main() {
	Input input{9};
	Target<long long> target = input;
	return 0;
}
