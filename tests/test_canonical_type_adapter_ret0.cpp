struct Sample {
	char small;
	double large;
};

enum class Access : unsigned short {
	Read = 3,
	Write = 5,
};

struct MemberHost {
	int field;
	long other;
};

decltype(static_cast<int MemberHost::*>(nullptr)) readMemberObject();

int readShort(const short* value);
int readShort(const short* value) { return *value; }
int readLong(long long& value) { return static_cast<int>(value); }
int readRecord(const Sample& value) { return value.small; }
int readAccess(Access value) { return static_cast<int>(value); }
int readRecordArray(const Sample (&value)[2]);
int readAccessArray(const Access (&value)[2]);
int readArray(int (&value)[2]) { return value[1]; }
int invoke(int (*callback)(const short*), const short* value) { return callback(value); }
int __stdcall scale(int value) { return value * 2; }
using StdcallCallback = int(__stdcall*)(int);
int invokeStdcall(StdcallCallback callback, int value) { return callback(value); }

template<typename T>
T identity(T value) { return value; }

struct PointerNode {
	int value;
};

PointerNode first_pointer_node{1};
PointerNode second_pointer_node{2};

void selectPointerFunction() {}
void otherPointerFunction() {}

struct ObjectPointerKey {
	PointerNode* node;
	int tag;
};

constexpr ObjectPointerKey object_pointer_key{&first_pointer_node, 7};
constexpr ObjectPointerKey other_object_pointer_key{&second_pointer_node, 7};

struct FunctionPointerKey {
	void (*function)();
	int tag;
};

constexpr FunctionPointerKey function_pointer_key{&selectPointerFunction, 9};
constexpr FunctionPointerKey other_function_pointer_key{&otherPointerFunction, 9};

template<ObjectPointerKey Key>
struct ObjectPointerReader {
	static int tag() { return Key.tag; }
};

template<FunctionPointerKey Key>
struct FunctionPointerReader {
	static int tag() { return Key.tag; }
};

struct PointerKeyUsers {
	ObjectPointerReader<object_pointer_key> object_key;
	ObjectPointerReader<other_object_pointer_key> other_object_key;
	FunctionPointerReader<function_pointer_key> function_key;
	FunctionPointerReader<other_function_pointer_key> other_function_key;
};

int main() {
	short narrow = 3;
	long long wide = 5;
	Sample record = {7, 9.5};
	Sample records[2];
	Access permissions[2];
	int values[2] = {11, 13};
	PointerKeyUsers pointer_keys;
	int pointer_result = ObjectPointerReader<object_pointer_key>::tag() +
		ObjectPointerReader<other_object_pointer_key>::tag() +
		FunctionPointerReader<function_pointer_key>::tag() +
		FunctionPointerReader<other_function_pointer_key>::tag();
	return readShort(&narrow) + readLong(wide) + readRecord(record) +
		readAccess(Access::Write) + readArray(values) + invoke(readShort, &narrow) +
		invokeStdcall(scale, 3) + identity(17) + pointer_result - 91;
}
