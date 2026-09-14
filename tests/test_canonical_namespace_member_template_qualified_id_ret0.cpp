// A member class template under a class in a namespace is reachable through
// the namespace-qualified registry key: ns::Outer::Box<T> instantiates, and
// the same declaration also answers the unqualified spelling used inside the
// namespace. Mixed native widths and a struct argument exercise the path.
namespace ns {
struct Outer {
	template<typename T>
	struct Box {
		T value;
	};
};

struct Payload {
	int weight;
};
}

int usePayloadBox(ns::Outer::Box<ns::Payload> boxed) {
	return static_cast<int>(boxed.value.weight);
}

int main() {
	ns::Outer::Box<short> narrow{7};
	ns::Outer::Box<ns::Payload> boxed{ns::Payload{9}};
	ns::Outer::Box<short> local{3};
	return static_cast<int>(narrow.value) +
		usePayloadBox(boxed) +
		static_cast<int>(local.value) - 19;
}
