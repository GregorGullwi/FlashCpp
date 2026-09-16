struct Payload {
	int value;
};

int select(void (*)(int)) {
	return 1;
}

int select(void (*)(double)) {
	return 2;
}

int select(void (*)(Payload)) {
	return 4;
}

int selectNoexcept(void (*)()) {
	return 10;
}

int selectNoexcept(void (*)() noexcept) {
	return 20;
}

int intResult = 0;
int doubleResult = 0;
int payloadResult = 0;

void takeInt(int value) {
	intResult = value;
}

void takeDouble(double value) {
	doubleResult = static_cast<int>(value * 2.0);
}

void takePayload(Payload value) {
	payloadResult = value.value;
}

void takePlain() {}
void takeNoexcept() noexcept {}

int invoke(void (*callback)(int)) {
	callback(17);
	return select(callback);
}

int invoke(void (*callback)(double)) {
	callback(3.5);
	return select(callback);
}

int invoke(void (*callback)(Payload)) {
	callback(Payload{5});
	return select(callback);
}

int main() {
	return invoke(takeInt) + invoke(takeDouble) + invoke(takePayload) +
		intResult + doubleResult + payloadResult +
		selectNoexcept(takePlain) + selectNoexcept(takeNoexcept) - 66;
}
