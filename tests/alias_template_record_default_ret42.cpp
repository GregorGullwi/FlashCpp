// C++20 [temp.alias]: a nominal type default on a namespace alias template is
// bound to its published EntityId before the direct alias target materializes,
// so S<> names the record/enum instead of an unresolved T placeholder. Cover
// value types of different sizes plus a scoped and unscoped enum default.
struct Small {
	short value;
};

struct Large {
	long first;
	short second;
};

enum PlainTag {
	PlainValue = 40
};

enum class ScopedTag : long {
	Value = 1
};

template <class T = Small> using SmallBox = T;
template <class T = Large> using LargeBox = T;
template <class T = PlainTag> using Tagged = T;
template <class T = ScopedTag> using Scoped = T;

SmallBox<> small{2};
LargeBox<> large{3, 4};
Tagged<> tag = PlainValue;
Scoped<> scoped = ScopedTag::Value;

int main() {
	if (tag != PlainValue || scoped != ScopedTag::Value) {
		return 1;
	}
	return small.value + large.first + large.second == 9 ? 42 : 2;
}
