// Test: __builtin_va_start works after heavy template usage
// Validates fix for pending_explicit_template_args_ leak

template<typename T>
struct Container {
    T value;
    Container() = default;
    Container(T v) : value(v) {}
};

template<typename T>
T identity(T v) { return v; }

// Force template instantiation
template struct Container<int>;
template struct Container<double>;

void varargs_func(const char* fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
}

int main() {
    Container<int> c(42);
    int x = identity(c.value);
    return x == 42 ? 0 : 1;
}
