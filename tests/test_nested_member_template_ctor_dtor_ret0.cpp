// Regression: a nested class template inside another struct can have a
// constructor-template and destructor that are re-parsed (and then skipped in
// the pattern-body parsing phase) without the parser rejecting them.
//
// Reduced from libstdc++ <optional>'s _Optional_payload_base::_Storage<_Up, bool>
// member-class template, which uses both a templated ctor that takes an
// `in_place_t` tag type and a user-provided destructor.

struct in_place_t { };

template <typename _Tp>
struct _Outer {
	struct _Empty_byte { };

	// Primary member class template
	template <typename _Up, bool = true>
	union _Storage {
		_Storage() noexcept : _M_empty() { }

		template <typename... _Args>
		_Storage(in_place_t, _Args&&...) : _M_value() { }

		~_Storage() { }

		_Empty_byte _M_empty;
		_Up _M_value;
	};

	// Partial specialization with its own templated constructor + destructor
	template <typename _Up>
	union _Storage<_Up, false> {
		_Storage() noexcept : _M_empty() { }

		template <typename... _Args>
		_Storage(in_place_t, _Args&&...) : _M_value() { }

		~_Storage() { }

		_Empty_byte _M_empty;
		_Up _M_value;
	};
};

int main() {
	// The test's main purpose is that the nested member-template ctor/dtor
	// declarations above are accepted by the parser. We don't attempt to
	// instantiate _Storage directly here because the parent struct is itself
	// a class template: mirror the libstdc++ pattern where _Storage is only
	// used as a data member of another template.
	return 0;
}
