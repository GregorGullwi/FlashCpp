template<typename... Args>
struct Tuple {};

template<typename T1, typename T2>
struct Tuple<T1, T2> {
    T1 first;
    T2 second;
};

int main() {
    Tuple<int, float> pair;
    pair.first = 10;
    pair.second = 3.14f;
    return pair.first;
}
