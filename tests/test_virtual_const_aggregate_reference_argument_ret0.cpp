struct Reading {
	virtual int value() const { return 37; }
};

struct Sample {
	short count;
	long long total;
};

struct Box {
	char tag;
	int payload;
};

const Reading reading;
constexpr const Reading* reading_pointer = &reading;
Sample sample = { 3, 9000000000LL };
Sample* sample_pointer = &sample;
Box box = { 2, 40 };
Box* box_pointer = &box;
int native_value = 5;
int* native_pointer = &native_value;

struct Inspector {
	virtual int inspect(const Reading& object, const Sample& data, const Box& packed, const int& value) const {
		return object.value() + static_cast<int>(data.count) + packed.payload + value + packed.tag;
	}
};

int main() {
	Inspector inspector;
	Inspector* inspector_pointer = &inspector;
	if (inspector_pointer->inspect(*reading_pointer, *sample_pointer, *box_pointer, *native_pointer) != 87) return 1;
	if (inspector.inspect(reading, sample, box, native_value) != 87) return 2;
	return 0;
}
