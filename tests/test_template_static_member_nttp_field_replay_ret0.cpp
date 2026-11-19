// Regression: a member access rooted at a dependent non-type template parameter
// object has no class owner to look up during structural substitution. The
// qualified lookup must stay deferred and let the definition-context replay bind
// the structural class value, instead of being accepted as a bound result.
//
// The reduced shape used to be covered only by the larger NTTP identity test.

struct Key {
	int value;
};

template <Key Value>
struct Holder {
	static constexpr int value = Value.value;
};

int main() {
	return Holder<Key{7}>::value == 7 ? 0 : 1;
}
