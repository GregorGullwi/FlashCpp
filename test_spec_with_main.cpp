template<typename T>
T identity(T x);

template<>
int identity<int>(int x) {
    return x;
}

int main() {
    return identity<int>(42);
}
