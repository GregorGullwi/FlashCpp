// Test: ns::f(t) with dependent arg t should defer instantiation in template body.
// Before fix 2, has_deferred_qualified_call_args did not gate try_instantiate_template,
// so the call could be bound eagerly instead of remaining dependent.
namespace compute {
template<typename T>
T add(T a, T b) { return a + b; }
}

template<typename T>
T computeSum(T x, T y) {
return compute::add(x, y);
}

int main() {
return computeSum(20, 22);  // 42
}
