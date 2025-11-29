// Test primary template only (no specialization)
template<typename T>
class Container {
public:
    int getType() {
        return 0;
    }
};

int main() {
    Container<int> ci;
    return ci.getType();
}

