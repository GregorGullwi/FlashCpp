struct ExactTag {};

template<typename... Ts>
struct Recursive;

template<>
struct Recursive<> {
	constexpr Recursive(ExactTag) {}
};

template<typename Head, typename... Tail>
struct Recursive<Head, Tail...> : Recursive<Tail...> {
	template<typename HeadArg, typename... TailArgs>
	constexpr Recursive(ExactTag, HeadArg&& head, TailArgs&&... tail)
		: Recursive<Tail...>(ExactTag{}, tail...), value(head) {}

	Head value;
};

int main() {
	ExactTag tag;
	Recursive<int, short, long> values(tag, 40, static_cast<short>(1), 1L);
	return values.value
		+ static_cast<Recursive<short, long>&>(values).value
		+ static_cast<Recursive<long>&>(values).value;
}
