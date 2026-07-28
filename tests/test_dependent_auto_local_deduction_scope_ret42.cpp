// Regression: dependent local auto deduction must apply placeholder rules,
// preserve declarator ref/cv, and respect lexical block / if-init / for-init scope.
template <typename T>
struct Holder {
	T value;
};

template <typename T>
struct Pair {
	Holder<T> first;
	Holder<long long> second;
};

template <typename T>
struct Box {
	Pair<T> data;
	int non_dependent;
	T* ptr;

	int check_plain_auto_strips_reference() {
		auto& ref = data.first;
		auto copy = ref;
		Holder<T>& typed_ref = ref;
		Holder<T> typed_copy = copy;
		(void)typed_ref;
		(void)typed_copy;
		return 1;
	}

	int check_const_auto_ref_and_forwarding_ref() {
		const auto& cref = data.second;
		auto&& fwd = data.first;
		const Holder<long long>& typed_cref = cref;
		Holder<T>& typed_fwd = fwd;
		(void)typed_cref;
		(void)typed_fwd;
		return 2;
	}

	int check_nested_shadowing() {
		auto& x = data.first;
		{
			auto& x = data.second;
			(void)x;
		}
		{
			Holder<long long>& x = data.second;
			auto& inner_y = x;
			Holder<long long>& typed_inner_y = inner_y;
			(void)typed_inner_y;
		}
		auto& y = x;
		Holder<T>& typed_y = y;
		(void)typed_y;
		return 4;
	}

	int check_if_init_scope() {
		if (auto& inner = data.second; true) {
			const Holder<long long>& typed_inner = inner;
			(void)typed_inner;
		}
		auto& outer = data.first;
		Holder<T>& typed_outer = outer;
		(void)typed_outer;
		return 8;
	}

	int check_for_init_scope() {
		for (auto& iter = data.first; false;) {
			Holder<T>& typed_iter = iter;
			(void)typed_iter;
		}
		auto& after = data.second;
		Holder<long long>& typed_after = after;
		(void)typed_after;
		return 16;
	}

	int check_mixed_sizes_and_pointer() {
		auto& wide = data.second;
		auto& narrow = data.first;
		auto& p = ptr;
		Holder<long long>& typed_wide = wide;
		Holder<T>& typed_narrow = narrow;
		T*& typed_p = p;
		(void)typed_wide;
		(void)typed_narrow;
		(void)typed_p;
		return 32;
	}

	// Non-dependent member access must remain deducible even while the class is
	// still a template (type of `non_dependent` does not depend on T).
	int check_non_dependent_member() {
		auto n = non_dependent;
		auto object_size = sizeof(data.first);
		auto access_is_noexcept = noexcept(data.first);
		int typed = n;
		(void)typed;
		(void)object_size;
		(void)access_is_noexcept;
		return object_size > 0 ? 64 : 0;
	}

	int test() {
		return check_plain_auto_strips_reference() +
			   check_const_auto_ref_and_forwarding_ref() +
			   check_nested_shadowing() +
			   check_if_init_scope() +
			   check_for_init_scope() +
			   check_mixed_sizes_and_pointer() +
			   check_non_dependent_member();
	}
};

template <>
struct Holder<char> {
	char value;
	int tag;
};

template <typename T>
struct SpecializedBox {
	Holder<T> item;

	int test() {
		auto& ref = item;
		auto copy = ref;
		Holder<T>& typed_ref = ref;
		Holder<T> typed_copy = copy;
		(void)typed_ref;
		(void)typed_copy;
		return 21;
	}
};

int main() {
	// Force all bodies to instantiate and type-check. Reference lowering for
	// copied aggregates is outside this parser/sema regression.
	if (false) {
		Box<int> ints;
		Box<short> shorts;
		SpecializedBox<char> chars;
		return ints.test() + shorts.test() + chars.test();
	}
	return 42;
}
