// C++20 [stmt.assert]/1 permits static_assert without a message.
// An explicitly empty string literal is still a valid message and must not be
// confused with the message-less form while parsing the declaration.

struct AssertionForms {
	static_assert(true);
	static_assert(true, "");
};

int main() {
	return sizeof(AssertionForms) == 1 ? 0 : 1;
}
