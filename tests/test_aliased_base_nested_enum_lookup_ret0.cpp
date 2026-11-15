// A base reached through a type alias must stay in the hierarchy walk so its
// nested members remain visible to member-context lookup.

struct AliasedBaseEnum {
	enum class Kind {
		value = 42,
	};
};

struct SiblingBase {};

using AliasedBase = AliasedBaseEnum;

struct DerivedFromAlias : AliasedBase, SiblingBase {
	int lookup() {
		return static_cast<int>(Kind::value);
	}
};

int main() {
	DerivedFromAlias derived;
	return derived.lookup() == 42 ? 0 : 1;
}
