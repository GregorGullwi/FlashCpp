// Test member function template with only return statement

class Container {
public:
    template<typename U>
    int getValue(U item) {
        return 42;
    }
};

int main() {
    Container c;
    int result = c.getValue(10);
    return result;
}
