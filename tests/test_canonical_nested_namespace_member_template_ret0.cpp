// Member class templates under classes in nested namespaces are reachable
// through every legal spelling: the fully qualified chain, the partial
// namespace chain visible through a using-directive, and the bare chain used
// inside the namespace. Mixed native widths and a struct argument exercise
// the path.
namespace ns {
namespace inner {
struct Outer {
	struct Inner {
		template<typename T>
		struct Box {
			T value;
		};
	};
};

struct Payload {
	int weight;
};
}
}

using namespace ns;

int usePayloadBox(inner::Outer::Inner::Box<ns::inner::Payload> boxed) {
	return static_cast<int>(boxed.value.weight);
}

int main() {
	ns::inner::Outer::Inner::Box<short> narrow{7};
	ns::inner::Outer::Inner::Box<ns::inner::Payload> boxed{ns::inner::Payload{9}};
	inner::Outer::Inner::Box<short> local{3};
	return static_cast<int>(narrow.value) +
		usePayloadBox(boxed) +
		static_cast<int>(local.value) - 19;
}
