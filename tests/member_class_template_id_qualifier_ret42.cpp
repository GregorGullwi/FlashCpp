// C++20 [temp.names]: a member class template-id is a valid nested-name-
// specifier, so a qualified type-id such as Outer<int>::Rebind<char>::type
// resolves through the instantiated member template body. Cover the explicit
// `template` keyword, a member template nested in a member template, a member
// typedef, and unqualified and namespace-qualified owners.
struct Payload {
	short tag;
};

template <class T>
struct Outer {
	template <class U>
	struct Rebind {
		using type = U;
		using record_type = Payload;
		template <class V>
		struct Deeper {
			using type = V;
		};
	};
};

namespace ns_scope {
template <class T>
struct Outer {
	template <class U>
	struct Rebind {
		using type = U;
	};
};
}  // namespace ns_scope

using IntAlias = Outer<int>::Rebind<char>::type;
using ExplicitAlias = Outer<int>::template Rebind<short>::type;
using DeepAlias = Outer<int>::Rebind<char>::Deeper<long>::type;
using RecordAlias = Outer<int>::Rebind<char>::record_type;
using NsAlias = ns_scope::Outer<int>::Rebind<char>::type;

IntAlias int_value = 10;
ExplicitAlias short_value = 11;
DeepAlias deep_value = 12L;
RecordAlias record_value{3};
NsAlias ns_value = 6;

int main() {
	const long total =
		int_value + short_value + deep_value + record_value.tag + ns_value;
	return total == 42 ? 42 : static_cast<int>(total);
}
