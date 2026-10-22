template<class Left, class Right> struct Pair { using type = Left; };
template<class Left, class Right> using First = typename Pair<Left, Right>::type;
template<class Result, class Input> using Forwarded = First<Result, Input>;
struct Right { short value; };
struct Other { long long value; };
enum class Left : unsigned char { value = 1 };
using RenamedRight = Right;
static_assert(__is_same(First<Right, Other>, Right));
static_assert(__is_same(First<RenamedRight, Other>, Right));
static_assert(__is_same(First<Left, Other>, Left));
static_assert(__is_same(Forwarded<Right, Other>, Right));
static_assert(__is_same(First<short, Right>, short));
int main() {
	First<Right, Other> selected{};
	selected.value = 3;
	return sizeof(selected) == sizeof(Right) && selected.value == 3 && __is_same(Forwarded<Right, Other>, Right) ? 0 : 1;
}
