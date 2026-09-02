template<typename T>
struct ExceptionPayload {
	T value;
};

void throwAcrossSections(int kind);
void relayAcrossSections(int kind);
