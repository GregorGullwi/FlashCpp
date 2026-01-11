// Test member alias template being used as identifier within same struct
// Tests the fix for looking up member alias templates with qualified names

template<typename T>
T declval();

struct Container {
    // Member alias template
    template<typename T, typename U>
    using cond_t = decltype(true ? declval<T>() : declval<U>());
    
    // Use cond_t within the same struct in another member alias
    template<typename T, typename U>
    using result_type = cond_t<T, U>;
};

// Test that we can use the outer type alias
template<typename T, typename U>
struct UseContainer {
    using type = typename Container::result_type<T, U>;
};

int main() {
    // Just verify compilation works
    return 0;
}
