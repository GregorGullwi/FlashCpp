template<typename T>
struct TypeIdentity {
	using type = T;
};

template<typename Ptr, typename Up>
struct ReplaceFirstArg {
	using type = Up;
};

template<typename Default, template<typename, typename> typename Op, typename Ptr, typename Up>
using DetectedOrT = TypeIdentity<Default>;

template<typename Ptr>
struct PointerTraits {
	template<typename Tp, typename Up>
	using RebindProbe = TypeIdentity<typename Tp::template rebind<Up>>;

	template<typename Up>
	using Rebind = typename DetectedOrT<ReplaceFirstArg<Ptr, Up>, RebindProbe, Ptr, Up>::type;
};

int main() {
	PointerTraits<int>::Rebind<char> value{};
	return sizeof(value) == sizeof(ReplaceFirstArg<int, char>) ? 0 : 1;
}
