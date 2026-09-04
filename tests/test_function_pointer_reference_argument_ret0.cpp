struct Reading {
	virtual int value() const { return 37; }
};

struct Sample {
	short count;
	long long total;
};

const Reading reading;
constexpr const Reading* reading_pointer = &reading;
Sample sample = { 3, 9000000000LL };
Sample* sample_pointer = &sample;
int native_value = 5;
int* native_pointer = &native_value;
char flag = 2;
char* flag_pointer = &flag;

int read(const Reading& object) {
	return object.value();
}

int inspect(const Reading& object, const Sample& data, const int& value, const char& mark) {
	return object.value() + static_cast<int>(data.count) + value + mark;
}

void update(Sample& object, int& value, char& mark) {
	object.count = 7;
	object.total = 9000000001LL;
	value = 11;
	mark = 4;
}

template<class T>
void store(T& target, T value) {
	target = value;
}

struct Holder {
	int (*read_fn)(const Reading&);
	void (*store_int)(int&, int);
};

int main() {
	int (*read_fn)(const Reading&) = &read;
	if (read_fn(*reading_pointer) != 37) return 1;

	int (*inspect_fn)(const Reading&, const Sample&, const int&, const char&) = &inspect;
	if (inspect_fn(*reading_pointer, *sample_pointer, *native_pointer, *flag_pointer) != 47) return 2;

	void (*update_fn)(Sample&, int&, char&) = &update;
	update_fn(*sample_pointer, *native_pointer, *flag_pointer);
	if (sample.count != 7 || sample.total != 9000000001LL) return 3;
	if (native_value != 11 || flag != 4) return 4;

	void (*store_fn)(int&, int) = &store<int>;
	store_fn(*native_pointer, 17);
	if (native_value != 17) return 5;

	Holder holder = { &read, &store<int> };
	if (holder.read_fn(*reading_pointer) != 37) return 6;
	holder.store_int(*native_pointer, 19);
	if (native_value != 19) return 7;

	return 0;
}
