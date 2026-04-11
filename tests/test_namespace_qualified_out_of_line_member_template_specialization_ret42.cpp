namespace ns {
template <typename T>
struct Container {
	template <typename U>
	int convert(U);
};

template <>
template <>
int Container<int>::convert<double>(double) {
	return 20;
}
}

template <>
template <>
int ns::Container<long>::convert<char>(char) {
	return 22;
}

int main() {
	ns::Container<int> int_container;
	ns::Container<long> long_container;
	return int_container.convert<double>(0.0) + long_container.convert<char>('x');
}
