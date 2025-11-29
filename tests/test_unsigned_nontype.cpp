template<unsigned int U>
struct UnsignedParam {
    unsigned int value = U;
};

int main() {
    UnsignedParam<100U> up;
    return up.value == 100U ? 0 : 1;
}