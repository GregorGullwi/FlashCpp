// Test member struct template partial specialization with inheritance
// More comprehensive test based on actual type_traits pattern

class MakeUnsigned {
public:  // Changed to public to allow access in main()
    // Primary template - empty list
    template<typename...> 
    struct List { 
        static constexpr int size = 0;
    };

    // Partial specialization - recursive list with inheritance and static constexpr
    template<typename Tp, typename... Up>
    struct List<Tp, Up...> : List<Up...> {
        static constexpr int size = sizeof(Tp);
    };
};

int main() {
    // Test instantiation - verify partial specialization with static constexpr compiles
    MakeUnsigned::List<int> list1;
    MakeUnsigned::List<int, char> list2;
    
    // Access static constexpr member directly from template instantiation
    // MakeUnsigned::List<int, char>::size should be sizeof(int) = 4
    return MakeUnsigned::List<int, char>::size;
}
