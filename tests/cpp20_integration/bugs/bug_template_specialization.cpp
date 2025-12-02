// Bug: Template specialization causes parser errors
// Status: PARSE ERROR - FlashCpp fails to parse template<> syntax
// Date: 2025-12-02
//
// Minimal reproduction case for FlashCpp parser error when using
// explicit template specialization.

template<typename T>
class Container {
public:
    T value;
    
    Container(T v) : value(v) {}
    
    T getValue() const {
        return value;
    }
};

// This explicit specialization causes parser error
template<>
class Container<bool> {
public:
    bool value;
    
    Container(bool v) : value(v) {}
    
    bool getValue() const {
        return !value;  // Invert for bool specialization
    }
};

int main() {
    Container<int> int_container(42);
    Container<bool> bool_container(true);
    
    int i = int_container.getValue();
    bool b = bool_container.getValue();
    
    return (i == 42) && (b == false) ? 0 : 1;
}

// Expected behavior (with clang++):
// Compiles and runs successfully, returns 0
//
// Actual behavior (with FlashCpp):
// Parser error:
// [INFO][Parser] error: Expected '{' or ';' after member function declaration
//
// Workaround:
// Avoid explicit template specialization, use if constexpr or other alternatives
