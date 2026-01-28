// Test: Multiple template parameters with defaults
// Container c; should use both defaults (int, char)

template<typename T = int, typename U = char>
struct Container {
    T first;
    U second;
};

int main() {
    Container c;           // Should be Container_int_char
    c.first = 42;
    c.second = 'X';
    
    Container<double> d;   // Should be Container_double_char (second defaults to char)
    d.first = 3.14;
    d.second = 'Y';
    
    return 0;
}
