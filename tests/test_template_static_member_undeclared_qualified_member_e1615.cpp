// A complete class owner with a published member table makes the negative
// member lookup conclusive. The structural static-initializer path must report
// the undeclared qualified member through the normal diagnostic path instead of
// accepting the result and failing later as a generic non-constant initializer.

struct Box {
	static constexpr int present = 1;
};

template <typename T>
struct Holder {
	static constexpr int value = Box::missing;
};

int main() {
	return Holder<int>::value;
}
