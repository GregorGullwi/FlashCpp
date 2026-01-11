// Comprehensive test for pack expansion support in various contexts

template<typename T>
T declval();

// ===== Part 1: Pack Expansion in Parenthesized Expressions =====

template<typename... Args>
struct test_paren {
    // Parenthesized pack expansion in decltype
    using type1 = decltype((declval<Args>()...));
};

// ===== Part 2: Pack Expansion with Pointer-to-Member (type_traits pattern) =====

template<typename Tp>
struct result_success {
    using type = Tp;
};

// Complex pattern from <type_traits> line 2499
struct test_ptrmem_pack {
    template<typename Fp, typename Tp1, typename... Args>
    static result_success<decltype(
        (declval<Tp1>().*declval<Fp>())(declval<Args>()...)
    )> test_call(int);
};

// ===== Part 3: Pack Expansion in Template Arguments =====

template<typename... Args>
struct tuple {
    // Empty for testing
};

template<typename... Args>
using tuple_of_refs = tuple<Args&...>;

// ===== Main Function =====

int main() {
    // Basic compilation test
    return 0;
}
