union Data {
    int i;
    float f;
    char c;
};

int main() {
    Data d;
    d.i = 42;
    int result = d.i;
    d.f = 3.14f;
    d.c = 'A';
    return result;
}
