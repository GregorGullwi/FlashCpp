class Widget {
    int value_;
public:
    Widget(int v) { value_ = v; }
    Widget(Widget& other) { value_ = other.value_; }
    Widget(Widget&& other) { value_ = other.value_; }
    int value() { return value_; }
};

void process_widget(Widget& w) {
}

void process_widget(Widget&& w) {
}

int main() {
    process_widget(Widget(42));
    return 0;
}
