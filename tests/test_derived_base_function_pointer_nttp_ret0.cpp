int function_pointer_nttp_target() {
	return 7;
}

template<int (*Callback)()>
struct FunctionPointerNttpBase {
	int value;
};

struct FunctionPointerNttpDerived : FunctionPointerNttpBase<&function_pointer_nttp_target> {
	int tag;
};

int main() {
	FunctionPointerNttpDerived object{};
	FunctionPointerNttpBase<&function_pointer_nttp_target>& base = object;
	base.value = 0;
	return base.value;
}
