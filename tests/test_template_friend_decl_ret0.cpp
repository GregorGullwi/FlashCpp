// Test template friend declarations parsing
// Tests the pattern: template<typename T1, typename T2> friend struct pair;
// This is used in <utility> header for std::pair

// Forward declaration of template
template<typename T1, typename T2>
struct pair;

// Template class with template friend declaration
template<typename U1, typename U2>
class __pair_base {
    // Template friend declaration - allows pair<T1,T2> to access private members
    template<typename T1, typename T2> friend struct pair;
    
    // Private members that pair should be able to access
    __pair_base() = default;
    ~__pair_base() = default;
};

// Also test friend class (non-template)
class Helper;

template<typename T>
class Container {
    friend class Helper;
    template<typename U> friend struct OtherTemplate;
};

int main() {
    return 0;
}
