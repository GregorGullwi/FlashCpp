template <class T>
int call_late_only(T value) {
return late_only(value);
}

int late_only(int value) {
return value;
}

int main() {
return call_late_only(42);
}
