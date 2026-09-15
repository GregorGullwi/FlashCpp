// Two direct member primaries with the same spelling are selected through
// instantiated outer owners. Their instance keys must retain the owner
// TemplateDeclId rather than sharing the legacy simple `Box` cache key.
template<typename T>
struct WideOwner {
	template<typename U>
	struct Box {
		T outer;
		U inner;

		int value() const {
			return static_cast<int>(sizeof(T) + sizeof(U));
		}
	};
};

template<typename T>
struct NarrowOwner {
	template<typename U>
	struct Box {
		U inner;

		int value() const {
			return static_cast<int>(sizeof(U));
		}
	};
};

struct Payload {
	int first;
	short second;
};

int main() {
	WideOwner<long long>::Box<Payload> wide{};
	NarrowOwner<char>::Box<Payload> narrow{};

	if (wide.value() != static_cast<int>(sizeof(long long) + sizeof(Payload))) {
		return 1;
	}
	if (narrow.value() != static_cast<int>(sizeof(Payload))) {
		return 2;
	}
	return sizeof(wide) == sizeof(long long) + sizeof(Payload) &&
		sizeof(narrow) == sizeof(Payload) ? 0 : 3;
}
