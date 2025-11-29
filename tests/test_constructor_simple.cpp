// Simplest possible constructor call test

extern "C" int printf(const char*, ...);

class Widget {
public:
    Widget(int v) {
        printf("Constructed %d\n", v);
    }
};

Widget make_widget() {
    return Widget(42);
}

int main() {
    make_widget();
    return 0;
}
