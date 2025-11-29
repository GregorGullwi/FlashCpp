template<char C>
struct CharParam {
    char value = C;
};

int main() {
    CharParam<'A'> cp;
    return cp.value == 'A' ? 0 : 1;
}