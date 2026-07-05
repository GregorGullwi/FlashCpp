struct owner {
	template <class... Args>
	struct list {};

	template <class... Args>
	struct list<Args...> {};
};

int main() {
	return 0;
}
