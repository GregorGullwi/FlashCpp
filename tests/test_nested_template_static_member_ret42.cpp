// Test case for issue #555: Nested template static member function handling
// Tests that >> tokens in nested templates are handled correctly in skip_member_declaration_to_semicolon()

// Simplified versions of std::vector and std::pair to avoid library dependencies
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
}

// Simple case: nested templates in static member function parameter
template<typename T>
struct Container {
    template<typename U>
    struct Nested {
        // The >> here causes angle_depth to only decrement by 1
        // instead of 2, leaving angle_depth at 1
        static int get_pair(std::pair<std::vector<U>, int> data) {
            return 42;
        }
        
        // This member should still be parsed correctly
        int important_member;
    };
};

// More complex case: Multiple nested template parameters
template<typename T>
struct Outer {
    template<typename U>
    struct Inner {
        // Static member function with nested template parameter
        // The >> in vector<vector<U>> won't be handled correctly without the fix
        static void process(std::vector<std::vector<U>> data) {
        }
        
        // Another member after - this might get incorrectly skipped
        int value;
    };
};

int main() {
    // We're not actually instantiating these templates, just testing that they parse
    return 42;
}
