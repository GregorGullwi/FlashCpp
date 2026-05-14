namespace detail {
	int value() {
		return 42;
	}
}

template<typename T>
int run() {
	return detail::value();
}

int main() {
	return run<int>();
}
