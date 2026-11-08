int pointer_nttp_target = 7;

template<int* Pointer>
struct PointerNttpBase {
	int value;
};

struct PointerNttpDerived : PointerNttpBase<&pointer_nttp_target> {
	int tag;
};

int main() {
	PointerNttpDerived object{};
	PointerNttpBase<&pointer_nttp_target>& base = object;
	base.value = 0;
	return base.value;
}
