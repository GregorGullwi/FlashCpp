// A member alias whose target is itself a template-id over an enclosing owner
// parameter and the alias's own parameter (Both<Owner, Value>) must redirect
// canonically with owner arguments followed by alias arguments. The
// materialized specialization keeps the expected member layout and sizes.
template <class First, class Second>
struct Both { First first; Second second; };

template <class Owner>
struct Captures {
	template <class Value>
	using Mixed = Both<Owner, Value>;
};

using M = typename Captures<int>::template Mixed<char>;

int main() {
	M value{};
	value.first = 41;
	value.second = 1;
	if (sizeof(value) != sizeof(Both<int, char>))
		return 0;
	if (sizeof(value.first) != sizeof(int))
		return 0;
	if (sizeof(value.second) != sizeof(char))
		return 0;
	return value.first + value.second;
}
