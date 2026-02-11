// Test: member variable template with partial specialization
// Verifies that template<...> constexpr Type name<pattern> = expr; is parsed
// This pattern appears in variant:831 for __accepted_index

template<typename... Types>
struct Variant {
    // Primary variable template
    template<typename T, typename V, typename = void>
    static constexpr int accepted_index = -1;

    // Partial specialization  
    template<typename T, typename V>
    static constexpr int accepted_index<T, V, void> = 0;
};

int main() {
    return 0;
}
