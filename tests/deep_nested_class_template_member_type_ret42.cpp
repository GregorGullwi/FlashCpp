// C++20 [temp.inst]: member typedefs of classes nested several levels deep are
// published under the instantiated owner chain, so a qualified type-id such as
// Outer<int>::Level1::Level2::type resolves at any depth, for a
// template-parameter-dependent, concrete, and record target, through both
// unqualified and namespace-qualified owners.
struct Payload {
	short tag;
};

template <class T>
struct Outer {
	struct Level1 {
		struct Level2 {
			using value_type = T;
			using fixed_type = int;
			struct Level3 {
				using record_type = Payload;
			};
		};
	};
};

namespace ns_scope {
template <class T>
struct Outer {
	struct Level1 {
		struct Level2 {
			using value_type = T;
		};
	};
};
}  // namespace ns_scope

using DeepValue = Outer<long>::Level1::Level2::value_type;
using DeepFixed = Outer<char>::Level1::Level2::fixed_type;
using DeepRecord = Outer<int>::Level1::Level2::Level3::record_type;
using NsDeepValue = ns_scope::Outer<int>::Level1::Level2::value_type;

DeepValue deep_value = 20L;
DeepFixed deep_fixed = 21;
DeepRecord deep_record{1};
NsDeepValue ns_deep_value = 0;

int main() {
	const long total =
		deep_value + deep_fixed + deep_record.tag + ns_deep_value;
	return total == 42 ? 42 : static_cast<int>(total);
}
