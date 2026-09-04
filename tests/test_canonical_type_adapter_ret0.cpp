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
	if (readShort(&narrow) != 3) return 1;
	if (readLong(wide) != 5) return 2;
	if (readRecord(record) != 7) return 3;
	if (readArray(values) != 13) return 4;
	if (invoke(readShort, &narrow) != 3) return 5;
	if (identity(17) != 17) return 6;
	return 0;
}
