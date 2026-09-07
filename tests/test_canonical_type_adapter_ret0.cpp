struct Sample {
	char small;
	double large;
};

enum class Access : unsigned short {
	Read = 3,
	Write = 5,
};

int readShort(const short* value);
int readShort(const short* value) { return *value; }
int readLong(long long& value) { return static_cast<int>(value); }
int readRecord(const Sample& value) { return value.small; }
int readAccess(Access value) { return static_cast<int>(value); }
int readRecordArray(const Sample (&value)[2]);
int readAccessArray(const Access (&value)[2]);
int readArray(int (&value)[2]) { return value[1]; }
int invoke(int (*callback)(const short*), const short* value) { return callback(value); }

template<typename T>
T identity(T value) { return value; }

int main() {
	short narrow = 3;
	long long wide = 5;
	Sample record = {7, 9.5};
	Sample records[2];
	Access permissions[2];
	int values[2] = {11, 13};
	return readShort(&narrow) + readLong(wide) + readRecord(record) +
		readAccess(Access::Write) + readArray(values) + invoke(readShort, &narrow) + identity(17) - 53;
}
