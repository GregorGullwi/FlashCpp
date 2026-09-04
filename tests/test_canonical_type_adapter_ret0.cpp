struct Sample {
	char small;
	double large;
};

int readShort(const short* value);
int readShort(const short* value) { return *value; }
int readLong(long long& value) { return static_cast<int>(value); }
int readRecord(const Sample& value) { return value.small; }
int readArray(int (&value)[2]) { return value[1]; }
int invoke(int (*callback)(const short*), const short* value) { return callback(value); }

template<typename T>
T identity(T value) { return value; }

int main() {
	short narrow = 3;
	long long wide = 5;
	Sample record = {7, 9.5};
	int values[2] = {11, 13};
	return readShort(&narrow) + readLong(wide) + readRecord(record) +
		readArray(values) + invoke(readShort, &narrow) + identity(17) - 48;
}
