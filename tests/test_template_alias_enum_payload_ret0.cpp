template <typename T>
struct Box {
	enum class State { Off = 1,
					   On = 2 };

	using Alias = State;
	using AliasAgain = Alias;

	static AliasAgain get() {
		return AliasAgain::On;
	}
};

int main() {
	Box<int>::AliasAgain state = Box<int>::get();
	return static_cast<int>(state) == 2 ? 0 : 1;
}
