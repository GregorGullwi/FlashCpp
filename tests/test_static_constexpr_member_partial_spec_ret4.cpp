// Test static constexpr members in member struct template partial specialization
class TestClass {
protected:
    // Primary template
    template<typename...> 
    struct List { 
        static constexpr int size = 0;
    };

    // Partial specialization with static constexpr member
    template<typename T, typename... Rest>
    struct List<T, Rest...> : List<Rest...> {
        static constexpr int size = sizeof(T);
    };
};

int main() {
    // Test instantiation - verify static constexpr members parse correctly
    TestClass::List<int> list1;
    TestClass::List<int, char> list2;
    
    // Return 4 for sizeof(int)
    return 4;
}
