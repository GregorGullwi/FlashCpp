// Applying const to a callable alias must qualify the pointer object itself.
using Callback = int (*)(short);
using ConstCallback = const Callback;
int target(short value) { return value; }
int main() {
	ConstCallback callback = &target;
	callback = &target;
	return callback(1);
}
