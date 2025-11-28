// Debug template specialization
template<typename T>
class Container {
public:
    int getType() {
        return 0;
    }
};

template<>
class Container<int> {
public:
    int getType() {
        return 1;
    }
};

int main() {
    Container<int> ci;
    return ci.getType();
}

