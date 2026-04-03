template<typename T>
struct payload_base {
	bool engaged;
};

template<typename T>
struct optional_payload : payload_base<T> {
	bool _M_is_engaged() const {
		return static_cast<const payload_base<T>*>(this)->engaged;
	}
};

template<typename T>
struct optional_base : optional_payload<T> {
};

template<typename T>
struct optional : optional_base<T> {
	bool has_value() const {
		return this->_M_is_engaged();
	}
};

int main() {
	optional<int> o;
	o.engaged = true;
	return o.has_value() ? 0 : 1;
}
