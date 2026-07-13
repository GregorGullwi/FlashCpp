namespace unrelated {
struct Cpo {
	enum class State { none, selected, other };
};
}

namespace target {
struct Payload {
	short tag;
	long long value;
};

struct Cpo {
	enum class State { none, selected };

	template<typename T>
	static constexpr bool selected() {
		constexpr State strategy = State::selected;
		return strategy == State::selected && sizeof(T) != 0;
	}
};
}

int main() {
	return target::Cpo::selected<int>() &&
		target::Cpo::selected<target::Payload>() ? 0 : 1;
}
