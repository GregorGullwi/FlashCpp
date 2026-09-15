// Regression: a member class template must retain the published owner of each
// outer and inner parameter while its Spec-rooted dependent member types parse.
template<typename RootValue>
struct Root {
	template<typename Value>
	struct Rebind {
		using type = Value;
	};
};

template<typename Outer>
struct TemplateOwner {
	template<typename Inner>
	struct Box {
		typename Root<Inner>::template Rebind<Inner>::type inner_value;
	};
};

struct Payload {
	short value;
};

int main() {
	TemplateOwner<int>::Box<Payload> value{};
	value.inner_value.value = 7;
	return value.inner_value.value - 7;
}
