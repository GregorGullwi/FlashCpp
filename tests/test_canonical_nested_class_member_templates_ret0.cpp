// Member function templates nested directly in a published nested class keep
// declaration identity: the nested class publishes its EntityId before its
// body parses, so the member template publishes a TemplateDeclId under the
// nested class-owned OwnerId and its Type-kind parameters stamp canonically.
// A second nesting level reuses the same parse-time publication chain. Mixed
// native widths and a struct member exercise the published path.
struct Outer {
	struct Inner {
		struct Leaf {
			template<typename T>
			T twice(T value) {
				return static_cast<T>(value + value);
			}
		};

		template<typename T, typename U>
		U pick(T left, U right) {
			return static_cast<U>(left) + right;
		}
	};
	short tag;
};

struct Payload {
	int weight;
};

short combine(short offset, Payload item) {
	return static_cast<short>(offset + item.weight);
}

int main() {
	Outer::Inner inner;
	Outer::Inner::Leaf leaf;
	Payload item{11};
	const short picked = inner.pick(3, static_cast<short>(20));
	const short doubled = leaf.twice(static_cast<short>(3));
	return combine(picked, item) + static_cast<int>(doubled) - 40;
}
