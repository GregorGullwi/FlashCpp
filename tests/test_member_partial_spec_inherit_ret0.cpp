// Test member struct template partial specialization with inheritance
// More comprehensive test based on actual type_traits pattern

class MakeUnsigned {
protected:
    // Primary template - empty list
    template<typename...> 
    struct List { };

    // Partial specialization - recursive list
    template<typename Tp, typename... Up>
    struct List<Tp, Up...> : List<Up...> {
        static constexpr int size = sizeof(Tp);
    };
};

int main() {
    // Test instantiation
    MakeUnsigned::List<int> list1;
    MakeUnsigned::List<int, char> list2;
    
    return 0;
}
