template<typename Selected, typename Unused>
using select_t = Selected;

template<typename Selected, typename Unused>
using select_pointer_t = Selected*;

template<typename Selected, typename Unused>
using select_reference_t = Selected&;

template<typename Selected, typename Unused>
using select_const_pointer_t = const Selected*;

struct Payload {
	long long value;
};

template<typename T, typename Ignored>
struct AliasUser {
	select_t<T, Ignored> copy(select_t<T, Ignored> value) {
		return value;
	}

	select_pointer_t<T, Ignored> pointer(select_pointer_t<T, Ignored> value) {
		return value;
	}

	select_reference_t<T, Ignored> reference(select_reference_t<T, Ignored> value) {
		return value;
	}

	select_const_pointer_t<T, Ignored> constPointer(
		select_const_pointer_t<T, Ignored> value) {
		return value;
	}
};

int main() {
	AliasUser<int, short> native_user;
	int value = 17;
	if (native_user.copy(value) != 17) {
		return 1;
	}
	if (native_user.pointer(&value) != &value) {
		return 2;
	}
	native_user.reference(value) = 23;
	if (value != 23) {
		return 3;
	}
	if (native_user.constPointer(&value) != &value) {
		return 4;
	}

	AliasUser<Payload, unsigned char> struct_user;
	Payload payload{41};
	Payload copied = struct_user.copy(payload);
	if (copied.value != 41) {
		return 5;
	}
	if (struct_user.pointer(&payload)->value != 41) {
		return 6;
	}
	struct_user.reference(payload).value = 47;
	if (payload.value != 47) {
		return 7;
	}
	if (struct_user.constPointer(&payload)->value != 47) {
		return 8;
	}
	return 0;
}
