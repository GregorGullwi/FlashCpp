class Widget {
    int value_;
public:
    Widget(int v) { value_ = v; }
    int value() { return value_; }
};

void process_widget(Widget& w) {
}

void process_widget(Widget&& w) {
}

int main() {
    return 0;
}
