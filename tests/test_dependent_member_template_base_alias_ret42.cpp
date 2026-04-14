template <class Callable, class... Args>
struct SelectInvokeTraits {
	template <class Rx>
	struct Box {};

	template <class Rx>
	using IsNothrowInvocableR = Box<Rx>;
};

template <class Rx, class Callable, class... Args>
struct IsNothrowInvocableR
	: SelectInvokeTraits<Callable, Args...>::template IsNothrowInvocableR<Rx> {};

int main() {
	IsNothrowInvocableR<int, void, float> instance;
	(void)instance;
	return 42;
}

