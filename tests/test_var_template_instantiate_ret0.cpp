// Test variable template instantiation in expression

template<typename T>
T pi = T(3.14159265358979323846);

int main() {
    auto x = pi<float>;
    return 0;
}
