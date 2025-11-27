// Test nested template usage
// Simplified version that tests template class with member access
template<typename T>
class Wrapper {
public:
    T data;
    
    T getValue() {
        return data;
    }
};

int main() {
    Wrapper<int> w;
    w.data = 42;
    int x = w.getValue();
    return x - 42;
}
