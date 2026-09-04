struct Reading {
	virtual int value() const { return 37; }
};

const Reading reading;
constexpr const Reading* reading_pointer = &reading;
const int native_value = 5;
constexpr const int* native_pointer = &native_value;
constexpr const char* label = "relro";

int read(const Reading* object) {
	return object->value();
}

int main() {
	if (read(reading_pointer) != 37) return 1;
	if (*native_pointer != 5) return 2;
	if (label[0] != 'r' || label[4] != 'o') return 3;
	return 0;
}
