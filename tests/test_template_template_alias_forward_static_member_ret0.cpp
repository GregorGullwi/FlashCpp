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
	return Forward<Box>::result::result::value - static_cast<int>(sizeof(int));
}
