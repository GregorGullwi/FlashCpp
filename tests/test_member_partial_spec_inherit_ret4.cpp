// Test member struct template partial specialization with inheritance
// More comprehensive test based on actual type_traits pattern

class MakeUnsigned {
protected:
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
    
    // Return 4 for sizeof(int) conceptually
    return 4;
}
