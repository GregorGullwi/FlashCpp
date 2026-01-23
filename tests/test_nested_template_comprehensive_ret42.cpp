// Comprehensive test for nested templates with >> in static member functions
// Verifies the fix for issue #555

namespace std {
    template<typename T>
    struct vector {
        T* data;
        int size;
    };
    
    template<typename T, typename U>
    struct pair {
        T first;
        U second;
    };
    
    template<typename T, typename U, typename V>
    struct tuple {
        T first;
        U second;
        V third;
    };
}

// Test 1: Basic nested template in static function parameter
template<typename T>
struct Test1 {
    template<typename U>
    struct Inner {
        static void func1(std::vector<std::vector<U>> param) {}
        int member1;  // Should be parsed correctly
    };
};

// Test 2: Multiple levels of nesting
template<typename T>
struct Test2 {
    template<typename U>
    struct Inner {
        static void func2(std::vector<std::vector<std::vector<U>>> param) {}
        int member2;  // Should be parsed correctly
    };
};

// Test 3: Nested templates in return type
template<typename T>
struct Test3 {
    template<typename U>
    struct Inner {
        static std::pair<std::vector<U>, int> func3() { return {}; }
        int member3;  // Should be parsed correctly
    };
};

// Test 4: Mix of nested templates in parameters
template<typename T>
struct Test4 {
    template<typename U>
    struct Inner {
        static void func4(
            std::vector<std::vector<U>> a,
            std::pair<std::vector<T>, std::vector<U>> b
        ) {}
        int member4;  // Should be parsed correctly
    };
};

// Test 5: Triple nesting with tuple
template<typename T>
struct Test5 {
    template<typename U>
    struct Inner {
        static void func5(std::tuple<std::vector<U>, std::vector<T>, int> param) {}
        int member5;  // Should be parsed correctly
    };
};

int main() {
    return 42;
}
