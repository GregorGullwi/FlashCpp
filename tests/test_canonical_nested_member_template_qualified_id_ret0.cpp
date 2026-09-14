// Member class templates nested in a nested class are reachable through the
// full owner chain: Outer::Inner::Box<T> instantiates through the qualified
// registry key, keeping the same declaration identity as the simple member
// spelling. Mixed native widths and a struct argument exercise the path.
struct Outer {
	struct Inner {
		template<typename T>
		struct Box {
			T value;
		};

		template<typename T, typename U>
		U pick(T left, U right) {
			return static_cast<U>(left) + right;
		}
	};
};

struct Payload {
	int weight;
};

int usePayloadBox(Outer::Inner::Box<Payload> boxed) {
	return static_cast<int>(boxed.value.weight);
}

int main() {
	Outer::Inner::Box<short> narrow{7};
	Outer::Inner::Box<Payload> boxed{Payload{9}};
	Outer::Inner inner;
	const short picked = inner.pick(3, static_cast<short>(17));
	return static_cast<int>(narrow.value) +
		usePayloadBox(boxed) +
		static_cast<int>(picked) - 36;
}
