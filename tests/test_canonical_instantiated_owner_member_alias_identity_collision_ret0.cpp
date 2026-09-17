// Two direct member alias primaries with the same spelling are selected
// through instantiated outer owners. Identity must resolve each owner's
// published TemplateDeclId rather than letting same-spelling registry keys
// share a materialization. Targets depend only on the alias parameter so
// outer-class binding is not required to prove owner selection.
template <typename T>
struct WideOwner {
	template <typename U>
	using Meter = char;
};

template <typename T>
struct NarrowOwner {
	template <typename U>
	using Meter = U;
};

struct Payload {
	long long first;
	long long second;
	long long third;
};

int main() {
	WideOwner<long long>::Meter<Payload> wide{};
	NarrowOwner<char>::Meter<Payload> narrow{};

	if (sizeof(wide) != sizeof(char)) {
		return 1;
	}
	if (sizeof(narrow) != sizeof(Payload)) {
		return 2;
	}
	if (sizeof(wide) == sizeof(narrow)) {
		return 3;
	}
	return static_cast<int>(wide) + static_cast<int>(narrow.first);
}
