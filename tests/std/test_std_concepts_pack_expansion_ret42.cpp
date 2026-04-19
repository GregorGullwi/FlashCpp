// Test: pack expansion in requires expression (mock of concepts:254)

template <typename _FTy, typename... _ArgTys>
concept invocable = requires(_FTy&& _Fn, _ArgTys&&... _Args) { 
	_Fn(_Args...); 
};

struct Fun {
	int operator()(int a, int b) const {
		return a + b;
	}
};

static_assert(invocable<Fun, int, int>);

int main() {
	Fun f;
	return f(20, 22);
}