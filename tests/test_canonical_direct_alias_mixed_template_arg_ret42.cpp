// A direct alias with a trailing type parameter and a template-template
// parameter must still redirect when the type argument is dependent on the
// enclosing template and the template-template argument is a concrete
// published primary. The concrete TemplateDeclId must survive the dependent
// alias identity path rather than degrading to a spelling placeholder.
template <class T>
struct Unary {
	using type = T;
};

template <class T, template <class> class C>
using Selected = T;

template <class U>
struct Holder {
	Selected<U, Unary> value;
};

int main() {
	Holder<char> holder = {};
	return sizeof(holder.value) == sizeof(char) ? 42 : 0;
}
