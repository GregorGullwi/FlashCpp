template<typename T>
struct derived {
	using value_type = int;
	typename derived<T>::value_type value{};

	int run() {
		value = 42;
		return value - 42;
	}
};

int main() {
	derived<int> d;
	return d.run();
}
