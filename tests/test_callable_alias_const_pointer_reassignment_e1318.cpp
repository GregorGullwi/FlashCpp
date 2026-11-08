// A genuinely const-qualified callable pointer object must reject reassignment.
using Callback = int (* const)(short);
int target(short value) { return value; }
int main() {
	Callback callback = &target;
	callback = &target;
	return callback(1);
}
