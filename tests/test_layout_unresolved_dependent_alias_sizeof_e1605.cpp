template <typename T>
struct AliasConsumer {
	using Missing = typename T::missing_type;
	static constexpr unsigned long long size = sizeof(Missing);
};

struct CompleteObject {
	int value;
};

int main() {
	return static_cast<int>(AliasConsumer<CompleteObject>::size);
}
