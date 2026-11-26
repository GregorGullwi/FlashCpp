// Storing the result of a dependent member-pointer trailing return type must
// keep the owner and the full member offset. A truncated projection makes the
// stored pointer address the wrong subobject; this is only visible when the
// owner is not the primary base at offset zero, so both layouts are covered.

struct StoredPrefix {
	int before;
	int after;
};

struct StoredBase {
	int code;
	long wide;
};

struct StoredLayout : StoredPrefix, StoredBase {};

template <class T>
auto stored_identity(T StoredBase::* pointer) -> T StoredBase::* {
	return pointer;
}

template <class T>
auto stored_deduced_identity(T StoredBase::* pointer) -> T StoredBase::* {
	return pointer;
}

int main() {
	StoredLayout object{};
	object.before = 11;
	object.after = 22;
	object.code = 33;
	object.wide = 44;

	int StoredBase::* code = stored_identity<int>(&StoredBase::code);
	long StoredBase::* wide = stored_identity<long>(&StoredBase::wide);

	if (object.before != 11 || object.after != 22) {
		return 1;
	}
	if (object.*code != 33) {
		return 2;
	}
	if (object.*wide != 44) {
		return 3;
	}

	// The deduced call must store the same owner and offset.
	int StoredBase::* deduced_code = stored_deduced_identity(code);
	long StoredBase::* deduced_wide = stored_deduced_identity(wide);
	if (object.*deduced_code != 33 || object.*deduced_wide != 44) {
		return 4;
	}

	// Writing through the stored pointers verifies the offset is still a real
	// member offset rather than a projected plain-pointer address.
	object.*code = 77;
	object.*wide = 88;
	if (object.code != 77 || object.wide != 88) {
		return 5;
	}
	if (object.before != 11 || object.after != 22) {
		return 6;
	}

	return 0;
}
