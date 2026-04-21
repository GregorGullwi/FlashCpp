template<typename T>
struct Identity {
	using type = T;
};

template<typename... Rest, typename U>
int should_fail(typename Identity<U>::type) {
	return 1;
}

int main() {
	return should_fail<int>(0);
}
