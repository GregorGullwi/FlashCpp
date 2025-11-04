// Simple class template
// Tests basic class template parsing (no instantiation yet)
template<typename T>
class Container {
public:
    T value;

    void set(T v) {
        value = v;
    }

    T get() {
        return value;
    }
};

int main() {
    return 0;
}

