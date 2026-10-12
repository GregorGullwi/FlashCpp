// Initializing a non-pointer scalar member from an array object cannot decay
// into the scalar, so it is rejected with ArrayToNonPointerMemberInitializer.

struct Holder {
	int value;
};

int main() {
	int elements[3] = {1, 2, 3};
	Holder holder{elements};
	return holder.value;
}
