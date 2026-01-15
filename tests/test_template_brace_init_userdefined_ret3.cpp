// Test brace initialization for instantiated template structs (Type::UserDefined)
// This tests the fix for brace initialization when templates are stored as Type::UserDefined

namespace ns {
    template<typename T>
    struct Pair {
        T first;
        T second;
    };
}

int main() {
    // Copy-list initialization of a template struct
    ns::Pair<int> p = {1, 2};
    
    // Direct-list initialization
    ns::Pair<int> p2{3, 4};
    
    // Verify values are correct
    int sum = p.first + p.second;  // 1 + 2 = 3
    return sum;
}
