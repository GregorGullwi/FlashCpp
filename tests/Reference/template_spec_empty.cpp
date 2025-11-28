// Test empty specialization
template<typename T>
class Container {
public:
    int value;
};

template<>
class Container<int> {
public:
    int special_value;
};

int main() {
    Container<int> ci;
    ci.special_value = 42;
    return ci.special_value;
}

