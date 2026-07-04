template <class T>
concept HasMemberBegin = requires(T value) {
	value.begin();
};

struct BeginCpo {
	template <class T>
		requires HasMemberBegin<T>
	auto operator()(T& value) const {
		return value.begin();
	}
};

inline constexpr BeginCpo begin_cpo{};

template <class T>
concept Range = requires(T value) {
	begin_cpo(value);
};

template <class It>
struct Subrange {
	It first{};

	It begin() {
		return first;
	}
};

int main() {
	static_assert(Range<Subrange<int*>>, "CPO begin should satisfy range");
	return 0;
}
