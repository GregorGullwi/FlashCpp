// An exact template-parameter spelling must take precedence over stale type
// metadata when substituting a member type alias and a data member.

namespace library {
	template<typename... Types>
	struct ArgTypes {};

	template<typename Ty1>
	struct ArgTypes<Ty1> {
		using argument_type = Ty1;
	};

	template<typename Ty1, typename Ty2>
	struct ArgTypes<Ty1, Ty2> {
		using first_argument_type = Ty1;
		using second_argument_type = Ty2;
	};

	template<typename Ty1>
	struct Identity {
		using type = Ty1;
	};

	template<typename Ty1>
	using IdentityT = typename Identity<Ty1>::type;

	using Warmup = IdentityT<unsigned long long>;

	template<typename Ty1>
	struct AliasUser {
		using transformed = IdentityT<Ty1>;
	};

	using AliasWarmup = AliasUser<long long>;

	template<typename Ty1, typename Ty2>
	struct PairLike {
		using first_type = Ty1;
		using second_type = Ty2;

		Ty1 first;
		Ty2 second;
	};
}

int main() {
	library::PairLike<int, float> value{42, 3.5f};
	library::PairLike<int, float>::first_type copy = value.first;
	return copy == 42 && value.second == 3.5f ? 0 : 1;
}
