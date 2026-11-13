// A conversion function template whose bare return type is its single deduced
// type parameter must participate in explicit and implicit conversions.

struct TemplateConversion {
	template<class U>
	operator U() const {
		return U{};
	}
};

struct ExplicitTemplateConversion {
	template<class U>
	explicit operator U() const {
		return U{};
	}
};

struct RvalueTemplateConversion {
	template<class U>
	operator U() && {
		return U{};
	}
};

struct PointerTemplateConversion {
	template<class U>
	operator U*() const {
		return nullptr;
	}
};

struct DoublePointerTemplateConversion {
	template<class U>
	operator U**() const {
		return nullptr;
	}
};

struct ConstPointerTemplateConversion {
	template<class U>
	operator const U*() const {
		return nullptr;
	}
};

struct InnerConstPointerTemplateConversion {
	template<class U>
	operator U* const*() const {
		return nullptr;
	}
};

struct BaseTemplateConversion {
	template<class U>
	operator U() const {
		return U{};
	}
};

struct DerivedTemplateConversion : BaseTemplateConversion {};

struct VirtualBaseTemplateConversion {
	template<class U>
	operator U() const {
		return U{};
	}
};

struct VirtualDerivedTemplateConversion : virtual VirtualBaseTemplateConversion {};

template<class T>
struct DeferredBaseTemplateConversion {
	template<class U>
	operator U() const {
		return U{};
	}
};

template<class T>
struct DeferredDerivedTemplateConversion : DeferredBaseTemplateConversion<T> {};

int main() {
	int a = static_cast<int>(TemplateConversion{});
	int b = static_cast<int>(ExplicitTemplateConversion{});
	int c = static_cast<int>(RvalueTemplateConversion{});
	int d = TemplateConversion{};
	const TemplateConversion const_template;
	int e = static_cast<int>(const_template);
	int f = const_template;
	if (a != 0 || b != 0 || c != 0 || d != 0 || e != 0 || f != 0) {
		return 1;
	}
	int* p = static_cast<int*>(PointerTemplateConversion{});
	int** pp = static_cast<int**>(DoublePointerTemplateConversion{});
	int* q = PointerTemplateConversion{};
	int* bare_static = static_cast<int*>(TemplateConversion{});
	int* bare_implicit = TemplateConversion{};
	if (p != nullptr || pp != nullptr || q != nullptr || bare_static != nullptr || bare_implicit != nullptr) {
		return 2;
	}
	const int* const_static = static_cast<const int*>(ConstPointerTemplateConversion{});
	const int* const_implicit = ConstPointerTemplateConversion{};
	const int* pointee_const_static = static_cast<const int*>(PointerTemplateConversion{});
	const int* pointee_const_implicit = PointerTemplateConversion{};
	if (const_static != nullptr || const_implicit != nullptr || pointee_const_static != nullptr || pointee_const_implicit != nullptr) {
		return 3;
	}
	int* const* inner_const_static = static_cast<int* const*>(InnerConstPointerTemplateConversion{});
	int* const* inner_const_implicit = InnerConstPointerTemplateConversion{};
	if (inner_const_static != nullptr || inner_const_implicit != nullptr) {
		return 4;
	}
	int inherited_static = static_cast<int>(DerivedTemplateConversion{});
	int inherited_implicit = DerivedTemplateConversion{};
	if (inherited_static != 0 || inherited_implicit != 0) {
		return 5;
	}
	int virtual_static = static_cast<int>(VirtualDerivedTemplateConversion{});
	int virtual_implicit = VirtualDerivedTemplateConversion{};
	if (virtual_static != 0 || virtual_implicit != 0) {
		return 6;
	}
	int deferred_static = static_cast<int>(DeferredDerivedTemplateConversion<int>{});
	int deferred_implicit = DeferredDerivedTemplateConversion<int>{};
	if (deferred_static != 0 || deferred_implicit != 0) {
		return 7;
	}
	return 0;
}
