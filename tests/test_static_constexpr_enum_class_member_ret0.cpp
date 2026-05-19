enum class Strategy {
	Functor,
	Member
};

struct Invoker {
	static constexpr Strategy strategy = Strategy::Member;
};

static_assert(Invoker::strategy == Strategy::Member);

int main() {
	return 0;
}
