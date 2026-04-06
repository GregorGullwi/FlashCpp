namespace outer {
	namespace detail {
		template <typename T>
		struct Holder {
			T value;
		};
	}

	template <typename T>
	struct Box {
		detail::Holder<T> holder;

		explicit Box(T value)
			: holder{} {
			holder.value = value;
		}

		T get() const {
			return holder.value;
		}
	};
}

int main() {
	outer::Box<int> box(6);
	return box.get();
}
