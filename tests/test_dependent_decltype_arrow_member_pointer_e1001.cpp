template <class C, class M>
struct ArrowInvoker {
	static auto call(M C::* member, C* ptr) -> decltype(ptr->*member);
};

struct Node {
	int data;
};

using NodeIntResult = decltype(ArrowInvoker<Node, int>::call(nullptr, nullptr));

int main() {
	NodeIntResult* unused = nullptr;
	return unused == nullptr ? 0 : 1;
}
