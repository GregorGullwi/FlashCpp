// Test: nested template declarations inside member struct template bodies
// Verifies that template<typename> struct inside a member struct template body is parsed
// This pattern appears in variant:857 for _Multi_array::__untag_result

template<typename T>
struct Outer {
    template<typename U>
    struct Inner {
        // Nested template struct inside member struct template
        template<typename V>
        struct Nested {
            using type = V;
        };
    };
};

int main() {
    return 0;
}
