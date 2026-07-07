template <typename T>
struct Holder {
	int read(T value) {
		struct Local {
			struct Nested {
				T stored;
			};
		};

		typename Local::Nested nested{value};
		return nested.stored;
	}
};

int main() {
	Holder<int> holder;
	return holder.read(42) == 42 ? 0 : 1;
}
