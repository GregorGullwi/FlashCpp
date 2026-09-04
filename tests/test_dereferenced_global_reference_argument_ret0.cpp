struct Reading {
	virtual int value() const { return 37; }
};

const Reading reading;
constexpr const Reading* reading_pointer = &reading;

int read(const Reading& object) {
	return object.value();
}

struct Sample {
	short count;
	long long total;
};

Sample sample = { 3, 9000000000LL };
Sample* sample_pointer = &sample;
int native_value = 5;
int* native_pointer = &native_value;
int evaluations = 0;

Sample* nextSample() {
	++evaluations;
	return sample_pointer;
}

void update(Sample& object, int& value) {
	object.count = 7;
	object.total = 9000000001LL;
	value = 11;
}

struct Inspector {
	virtual int inspect(const Reading& object) const {
		return object.value();
	}
};

int readOnStack(int a, int b, int c, int d, int e, int f,
	const Reading& object, Sample& data, int& value) {
	data.count = 9;
	value = 13;
	return a + b + c + d + e + f + object.value();
}

template<class T>
void store(T& target, T value) {
	target = value;
}

int main() {
	if (read(*reading_pointer) != 37) return 1;
	update(*sample_pointer, *native_pointer);
	if (sample.count != 7 || sample.total != 9000000001LL) return 2;
	if (native_value != 11) return 3;
	Inspector inspector;
	Inspector* inspector_pointer = &inspector;
	if (inspector_pointer->inspect(*reading_pointer) != 37) return 4;
	if (readOnStack(1, 2, 3, 4, 5, 6, *reading_pointer, *sample_pointer, *native_pointer) != 58) return 5;
	if (sample.count != 9 || native_value != 13) return 6;
	store(*native_pointer, 17);
	if (native_value != 17) return 7;
	update(*nextSample(), *native_pointer);
	if (evaluations != 1 || sample.count != 7 || native_value != 11) return 8;
	return 0;
}
