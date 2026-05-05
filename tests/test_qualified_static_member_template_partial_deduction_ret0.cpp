template <class T>
struct Source {
	static T make() {
		return T{};
	}
};

struct Known {
	int value;
};

template <class T>
struct Sink {
	template <class KnownType, class DependentType>
	static KnownType select(KnownType known, DependentType dependent) {
		(void)dependent;
		return known;
	}

	static constexpr int parsed_while_dependent = static_cast<int>(sizeof(Sink::select(Known{0}, Source<T>::make()).value));

	static int run() {
		return parsed_while_dependent - 4;
	}
};

int main() {
	return Sink<int>::run();
}
