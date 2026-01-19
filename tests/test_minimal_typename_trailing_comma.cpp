// Minimal test for typename with trailing comma
template<typename T> struct remove_cv { using type = T; };

template<typename T,
         typename U = typename remove_cv<T>::type,
         int = 0>
struct Test {};

int main() {
    return 0;
}
