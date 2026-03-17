// Hidden friend function at global scope, called with a Widget argument.
// The call is valid because ADL: w is of type Widget (global namespace),
// so the associated namespace is the global namespace which contains get_value.
// Return value is 0 on success.
struct Widget {
int value;
friend int get_value(Widget& w) { return w.value; }
};
int main() {
Widget w;
w.value = 7;
return get_value(w) - 7;  // ADL finds get_value; 7 - 7 == 0
}
