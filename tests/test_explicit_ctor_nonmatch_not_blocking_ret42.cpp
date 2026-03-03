struct Source {
int value;
};

struct Target {
int value;

explicit Target(const Target& other) : value(other.value) {}
Target(const Source& src) : value(src.value) {}
};

int useTarget(Target t) {
return t.value;
}

int main() {
Source s{42};
return useTarget(s);
}
