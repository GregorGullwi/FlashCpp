namespace Library {
inline constexpr unsigned DynamicExtent = static_cast<unsigned>(-1);

template <unsigned Extent = DynamicExtent>
struct FirstDefault {
	static constexpr bool valid = Extent == DynamicExtent;
};

template <typename Type, unsigned Extent>
struct View;

template <typename Type, unsigned Extent = DynamicExtent>
struct View {
	static constexpr int result = Extent == DynamicExtent ? 42 : 0;
};
}

int main() {
	return Library::FirstDefault<>::valid
		? Library::View<int>::result
		: 0;
}
