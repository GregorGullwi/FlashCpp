template <typename T>
struct AliasBox {
	using value_type = T;

	value_type value;

	explicit AliasBox(value_type v)
		: value(v) {}

	value_type getInline() const {
		return value;
	}

	value_type getDeclOnly() const;
};

template <typename T>
typename AliasBox<T>::value_type AliasBox<T>::getDeclOnly() const {
	return value;
}

int main() {
	AliasBox<int> box(42);
	return (box.getInline() == 42 && box.getDeclOnly() == 42) ? 0 : 1;
}
