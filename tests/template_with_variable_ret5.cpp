// Test member function template with local variable

class Container {
public:
    template<typename U>
    int test(U item) {
        int local = 5;
        return local;
    }
};

int main() {
    Container c;
    int result = c.test(10);
    return result;
}
