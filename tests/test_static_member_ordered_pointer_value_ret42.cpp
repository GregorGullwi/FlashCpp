// A static data member with an interleaved ordered pointer type keeps its spine
// through qualified lookup, overload resolution, storage sizing, and
// global-load lowering. `StructStaticMember` only retains the flat projection,
// so the ordered shape is recovered from the member's declaration AST.
namespace outer {
struct NestedHolder {
	static inline int (*(*value)[3])[4] = nullptr;
};
}

struct Holder {
	static inline int (*(*int_value)[3])[4] = nullptr;
	static inline double (*(*double_value)[2])[3] = nullptr;
	static inline char (* const (*pointer_cv_value)[3])[4] = nullptr;
	static inline int plain_value = 5;
};

int consumes_int(int (*(*)[3])[4]) { return 7; }
int consumes_double(double (*(*)[2])[3]) { return 9; }
int consumes_cv(char (* const (*)[3])[4]) { return 11; }

int main() {
	int (*(*local)[3])[4] = Holder::int_value;
	if (sizeof(Holder::int_value) != sizeof(void*)) {
		return 1;
	}
	if (consumes_int(Holder::int_value) != 7) {
		return 2;
	}
	if (consumes_int(local) != 7) {
		return 3;
	}
	if (consumes_double(Holder::double_value) != 9) {
		return 4;
	}
	if (consumes_cv(Holder::pointer_cv_value) != 11) {
		return 5;
	}
	if (consumes_int(outer::NestedHolder::value) != 7) {
		return 6;
	}
	if (Holder::int_value != nullptr) {
		return 7;
	}
	if (Holder::plain_value != 5) {
		return 8;
	}
	return 42;
}
