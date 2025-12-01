class Widget {
public:
    int value;
};

int test_cast(Widget w) {
    Widget* p = (Widget*)&w;
    return p->value;
}

int main() {
    return 0;
}
