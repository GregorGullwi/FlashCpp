// A direct alias with a trailing type parameter and a non-type parameter must
// redirect when the non-type argument is a concrete bool/integral literal. The
// literal's call-site syntax node interns to a stable ExprId that the published
// alias identity retains, while the selected type argument is still
// substituted at instantiation.
template <class T, int N>
using First = T;

template <class T>
struct Holder {
	First<T, 7> value;
};

int main() {
	Holder<char> holder = {};
	return sizeof(holder.value) == sizeof(char) ? 42 : 0;
}
