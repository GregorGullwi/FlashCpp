class Widget {
public:
    int value;
    Widget(int v) : value(v) {}
};

Widget w_global(10);

int main() {
    Widget* p = (Widget*)0;
    return 0;
}
