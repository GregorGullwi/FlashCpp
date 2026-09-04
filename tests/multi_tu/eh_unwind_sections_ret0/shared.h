template<typename T>
struct ExceptionPayload {
	T value;
};

struct WidePayload {
	long long high;
	int low;
};

void throwAcrossSections(int kind);
void relayAcrossSections(int kind);
