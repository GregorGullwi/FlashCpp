// Two direct member variable-template primaries with the same spelling are
// selected through instantiated outer owners. Identity must resolve each
// owner's published TemplateDeclId rather than falling back to the bare
// member-name registry key shared by both owners.
template <typename T>
struct WideOwner {
	template <typename U>
	static constexpr int Meter = sizeof(T);
};

template <typename T>
struct NarrowOwner {
	template <typename U>
	static constexpr int Meter = sizeof(U);
};

struct Payload {
	long long first;
	long long second;
	long long third;
};

int main() {
	constexpr int wide = WideOwner<long long>::Meter<Payload>;
	constexpr int narrow = NarrowOwner<char>::Meter<Payload>;

	if (wide != static_cast<int>(sizeof(long long))) {
		return 1;
	}
	if (narrow != static_cast<int>(sizeof(Payload))) {
		return 2;
	}
	if (wide == narrow) {
		return 3;
	}
	return 0;
}
