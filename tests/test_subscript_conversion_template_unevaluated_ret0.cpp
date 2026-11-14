struct SignatureOnly {
	template<class T = int>
	operator short*() noexcept {
		static_assert(sizeof(T) == 1, "an unevaluated subscript needs only the conversion signature");
		return nullptr;
	}
};

struct Evaluated {
	int data[2];
	int calls;

	template<class T = void>
	operator int*() {
		++calls;
		return data;
	}
};

int main() {
	SignatureOnly signature_only;
	if (sizeof(0[signature_only]) != sizeof(short) || !noexcept(signature_only[0])) {
		return 1;
	}

	Evaluated evaluated{{11, 42}, 0};
	if (sizeof(evaluated[1]) != sizeof(int) || evaluated.calls != 0) {
		return 2;
	}
	return evaluated[1] == 42 && evaluated.calls == 1 ? 0 : 3;
}
