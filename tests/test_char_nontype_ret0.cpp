template<char C>
struct CharParam {
    char value = C;
};

// Expected return: 0
int main() {
    CharParam<'A'> cp;
    return cp.value == 'A' ? 0 : 1;
}