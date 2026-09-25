// C++20 [temp.inst]: a class declared inside a member class template is parsed
// and published, so `Outer<int>::Rebind<char>::Inner` resolves with its data
// members, member typedef, member function, and its own nested class.
struct Payload {
	short tag;
};

template <class T>
struct Outer {
	template <class U>
	struct Rebind {
		struct Inner {
			U value;
			Payload payload;
			using value_type = U;
			U twice() { return value + value; }
			struct Deep {
				U deep;
			};
		};
	};
};

using ValueAlias = Outer<int>::Rebind<long>::Inner::value_type;

Outer<int>::Rebind<int>::Inner inner{5, Payload{4}};
Outer<int>::Rebind<int>::Inner::Deep deep{20};
ValueAlias alias_value = 8L;

int main() {
	const long total = inner.twice() + inner.payload.tag +
		static_cast<long>(deep.deep) + alias_value;
	return total == 42 ? 42 : static_cast<int>(total);
}
