// Test member struct template
class Container {
public:
    template<typename T>
    struct Wrapper {
        T value;
        int getOffset() { return 42; }
    };
};

int main() {
    Container::Wrapper<int> w;
    w.value = 100;
    return w.getOffset();
}
