template <typename T>
using identity_t = T;

template <identity_t<decltype(nullptr)> Value>
struct Holder {
	static constexpr int value = 42;
};

int main() {
	return Holder<nullptr>::value == 42 ? 0 : 1;
}
