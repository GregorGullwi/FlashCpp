// Test member function template - direct instantiation without calls
// This tests that template bodies with member access generate correctly

class Container {
public:
    int value;
    
    template<typename U>
    void set(U item) {
        value = static_cast<int>(item);
    }
};

// Force instantiation
template void Container::set<int>(int);
template void Container::set<double>(double);

int main() {
    return 42;
}
