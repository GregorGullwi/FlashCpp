template<typename T>
struct payload_base {
	bool engaged;

	payload_base(bool value)
		: engaged(value) {}
};

template<typename T>
struct payload : payload_base<T> {
	payload(bool value)
		: payload_base<T>(value) {}
};

template<typename T, typename D = payload<T>>
struct wrapper_impl : D {
	wrapper_impl(bool value)
		: D(value) {}

	bool has_value() const {
		return static_cast<const D*>(this)->engaged;
	}
};

template<typename T>
struct wrapper : wrapper_impl<T> {
	wrapper(bool value)
		: wrapper_impl<T>(value) {}
};

int main() {
	wrapper<int> value(true);
	return value.has_value() ? 0 : 1;
}
