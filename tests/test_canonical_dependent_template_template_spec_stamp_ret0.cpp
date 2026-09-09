// A template-template parameter used as a class-template specialization
// argument must retain its owning template parameter identity, not its spelling.
template <typename T>
struct Box {
	static constexpr int value = sizeof(T);
};

template <template <typename> class Template>
struct Consumer {
	using result = Template<int>;
};

template <template <typename> class Template>
struct Forward {
	using result = Consumer<Template>;
};

int main() {
	Forward<Box>::result instance{};
	(void)instance;
	return 0;
}
