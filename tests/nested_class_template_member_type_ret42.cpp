// C++20 [temp.inst]: a member typedef of a nested class of an instantiated
// class template is published under the instantiated nested owner, so a
// qualified type-id such as Outer<int>::Inner::value_type resolves for both a
// concrete and a template-parameter-dependent target. Mix native widths with a
// record target.
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

using IntAlias = Outer<int>::Inner::fixed_type;
using LongAlias = Outer<long>::Inner::value_type;
using RecordAlias = Outer<char>::Holder::record_type;

Outer<short>::Inner::value_type inline_value = 1;

int main() {
	IntAlias fixed_value = 20;
	LongAlias long_value = 21L;
	RecordAlias record_value{1};
	const long total = fixed_value + long_value + record_value.tag + inline_value;
	return total == 43 ? 42 : static_cast<int>(total);
}
