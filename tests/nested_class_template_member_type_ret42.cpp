// C++20 [temp.inst]: a member typedef of a nested class of an instantiated
// class template is published under the instantiated nested owner, so a
// qualified type-id such as Outer<int>::Inner::value_type resolves for both a
// concrete and a template-parameter-dependent target. Cover an unqualified and
// a namespace-qualified owner, mixing native widths with a record target.
struct Payload {
	short tag;
};

template <class T>
struct Outer {
	struct Inner {
		using value_type = T;
		using fixed_type = int;
	};
	struct Holder {
		using record_type = Payload;
	};
};

namespace ns_scope {
template <class T>
struct Outer {
	struct Inner {
		using value_type = T;
		using fixed_type = int;
	};
};
}  // namespace ns_scope

using IntAlias = Outer<int>::Inner::fixed_type;
using LongAlias = Outer<long>::Inner::value_type;
using RecordAlias = Outer<char>::Holder::record_type;
using NsAlias = ns_scope::Outer<int>::Inner::fixed_type;

Outer<short>::Inner::value_type inline_value = 1;
ns_scope::Outer<short>::Inner::value_type ns_inline_value = 1;

int main() {
	IntAlias fixed_value = 18;
	LongAlias long_value = 20L;
	RecordAlias record_value{1};
	NsAlias ns_value = 1;
	const long total = fixed_value + long_value + record_value.tag + ns_value +
		inline_value + ns_inline_value;
	return total == 42 ? 42 : static_cast<int>(total);
}
