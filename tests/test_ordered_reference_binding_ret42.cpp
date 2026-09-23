// C++20 [dcl.init.ref]: a reference whose referred-to type is a non-projectable
// pointer/array interleaving binds through the ordered spine. The flat
// reference-to-array path cannot represent that referent, so the structural
// declarator records an outermost reference component.
struct Payload {
	int value;
	short tag;
};

int int_storage = 0;
Payload payload_storage = {7, 1};

int take_int(int (*(*(&ref))[2])[3]) {
	return reinterpret_cast<int*>(ref) == &int_storage ? 1 : 0;
}

int take_payload(Payload (*(*(&ref))[2])[3]) {
	return reinterpret_cast<Payload*>(ref) == &payload_storage ? 1 : 0;
}

int main() {
	int (*(*int_ptr)[2])[3] =
		reinterpret_cast<int (*(*)[2])[3]>(&int_storage);
	Payload (*(*payload_ptr)[2])[3] =
		reinterpret_cast<Payload (*(*)[2])[3]>(&payload_storage);

	int (*(*(&int_ref))[2])[3] = int_ptr;
	Payload (*(*(&payload_ref))[2])[3] = payload_ptr;

	if (reinterpret_cast<int*>(int_ref) != &int_storage) {
		return 1;
	}
	if (reinterpret_cast<Payload*>(payload_ref) != &payload_storage) {
		return 2;
	}
	if (!take_int(int_ptr) || !take_payload(payload_ptr)) {
		return 3;
	}
	if (!take_int(int_ref) || !take_payload(payload_ref)) {
		return 4;
	}
	return 42;
}
