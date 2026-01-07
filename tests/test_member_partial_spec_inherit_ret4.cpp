// Test member struct template partial specialization with inheritance
// More comprehensive test based on actual type_traits pattern

class MakeUnsigned {
protected:
    // Primary template - empty list
    template<typename...> 
    struct List { };

    // Partial specialization - recursive list with inheritance
    template<typename Tp, typename... Up>
    struct List<Tp, Up...> : List<Up...> {
    };
};

int main() {
    // Test instantiation - verify partial specialization with inheritance compiles
    MakeUnsigned::List<int> list1;
    MakeUnsigned::List<int, char> list2;
    
    // Return 4 to match the filename
    return 4;
}
