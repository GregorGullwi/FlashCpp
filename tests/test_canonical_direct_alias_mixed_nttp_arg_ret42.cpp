// A direct alias with a trailing type parameter and a non-type parameter must
// redirect when the non-type argument is a dependent expression. Its original
// AST interns to a stable ExprId that the published alias identity retains,
// while the selected type argument is still substituted at instantiation.
template <class T, int N>
using First = T;

template <class T, int N>
struct Holder {
	First<T, N> value;
};

int main() {
	Holder<char, 7> holder = {};
	return sizeof(holder.value) == sizeof(char) ? 42 : 0;
}
