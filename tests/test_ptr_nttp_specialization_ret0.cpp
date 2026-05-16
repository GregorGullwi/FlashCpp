// Test: NTTP pointer and function-pointer specializations
// Verifies that template full specializations keyed on object-pointer (&var)
// and function-pointer (&func) NTTP arguments are correctly selected at the
// call site, covering the identity round-trip through TypeInfo::TemplateArgInfo.

int ga = 1;
int gb = 2;

int fa() {
	return 3;
}

int fb() {
	return 4;
}

template <int* P>
struct ObjectPtrTag {
	static int value() {
		return -1;
	}
};

template <>
struct ObjectPtrTag<&ga> {
	static int value() {
		return 10;
	}
};

template <>
struct ObjectPtrTag<&gb> {
	static int value() {
		return 20;
	}
};

template <int (*F)()>
struct FunctionPtrTag {
	static int value() {
		return -1;
	}
};

template <>
struct FunctionPtrTag<&fa> {
	static int value() {
		return 30;
	}
};

template <>
struct FunctionPtrTag<&fb> {
	static int value() {
		return 40;
	}
};

int main() {
	if (ObjectPtrTag<&ga>::value() != 10) {
		return 1;
	}
	if (ObjectPtrTag<&gb>::value() != 20) {
		return 2;
	}
	if (FunctionPtrTag<&fa>::value() != 30) {
		return 3;
	}
	if (FunctionPtrTag<&fb>::value() != 40) {
		return 4;
	}
	return 0;
}
