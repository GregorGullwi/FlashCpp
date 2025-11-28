// Test member function templates
// A member function template is a template function inside a class (template or non-template)

// Case 1: Member function template in a non-template class
class Container {
public:
    int value;
    
    // Member function template - can accept any type
    template<typename U>
    void insert(U item) {
        value = static_cast<int>(item);
    }
};

// Case 2: Member function template in a template class
template<typename T>
class Vector {
public:
    T data;
    
    // Member function template - different from class template parameter
    template<typename U>
    void convert_and_set(U item) {
        data = static_cast<T>(item);
    }
};

int main() {
    // Case 1: Non-template class with template member function
    Container c;
    c.insert(42);        // insert<int>
    c.insert(3.14);      // insert<double>
    
    // Case 2: Template class with template member function
    Vector<int> v;
    v.convert_and_set(3.14);    // Vector<int>::convert_and_set<double>
    
    return 0;
}

