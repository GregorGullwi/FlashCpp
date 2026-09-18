// Regression: a published alias primary can be used with a dependent argument.
// The architecture unit regression mutation-validates the distinct canonical
// alias declaration identity; this source probe keeps the parser/materializer
// path covered for the supported single-primary form.
namespace alias_identity {
	template<class T>
	using project = T;
}

template<class T>
struct DependentAliasTemplateIdentity {
	using Value = alias_identity::project<T>;
};

int main() {
	using Result = DependentAliasTemplateIdentity<long>;
	return sizeof(Result::Value) == sizeof(long) ? 0 : 1;
}
