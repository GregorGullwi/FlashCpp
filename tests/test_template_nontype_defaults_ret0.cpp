// Regression test: template with non-type default parameters dependent on type params
// Previously caused heap-buffer-overflow in try_instantiate_class_template
// when non-type default evaluation failed to push_back to filled_template_args

template<int N>
struct my_val {
    static constexpr int value = N;
};

// Template with non-type defaults: triggers the code path that previously
// had out-of-bounds vector access when default evaluation fell through
template<typename T, int V = 0, int W = 0>
struct with_defaults {
    static constexpr int result = V + W;
};

// Specialization
template<typename T, int V>
struct with_defaults<T, V, 0> {
    static constexpr int result = V;
};

int main() {
    // Instantiate with only one arg - triggers default filling
    constexpr int r = with_defaults<int>::result;
    return r;  // Should return 0
}
