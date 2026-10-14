struct PointerReceiver {
	const void* value;
	PointerReceiver(const void* const& argument) noexcept : value(argument) {}
};

struct ArrayReceiver {
	int* value;
	ArrayReceiver(int* const& argument) noexcept : value(argument) {}
};

struct FunctionReceiver {
	int value;
	const void* argument_address;
	FunctionReceiver(int (* const& argument)()) noexcept : value(argument()), argument_address(&argument) {}
};

struct FunctionReferenceReceiver {
	int value;
	FunctionReferenceReceiver(int (&argument)()) noexcept : value(argument()) {}
};

struct AnswerSource {
	static int answer() { return 43; }
};

struct BoolReceiver {
	bool value;
	BoolReceiver(const bool& argument) noexcept : value(argument) {}
};

enum Small : short { negative = -19 };

struct EnumReceiver {
	long value;
	EnumReceiver(const long& argument) noexcept : value(argument) {}
};

int answer() { return 37; }

namespace Answers {
	int answer() { return 47; }
}

int main() {
	int values[3] = {11, 23, 31};
	int* pointer = values;
	PointerReceiver as_void(pointer);
	if (as_void.value != pointer) return 1;
	ArrayReceiver from_array(values);
	if (from_array.value != pointer || from_array.value[1] != 23) return 2;
	FunctionReceiver from_function(answer);
	if (from_function.value != 37) return 3;
	BoolReceiver as_bool(pointer);
	if (!as_bool.value) return 4;
	Small enumeration = negative;
	EnumReceiver from_enum(enumeration);
	if (from_enum.value != -19) return 5;
	PointerReceiver null_pointer(nullptr);
	if (null_pointer.value != nullptr) return 6;
	FunctionReferenceReceiver function_reference(answer);
	if (function_reference.value != 37) return 7;
	FunctionReceiver from_address(&answer);
	if (from_address.value != 37) return 8;
	int (*function_pointer)() = answer;
	FunctionReceiver from_pointer(function_pointer);
	if (from_pointer.value != 37 || from_pointer.argument_address != &function_pointer) return 9;
	FunctionReceiver from_static{AnswerSource::answer};
	if (from_static.value != 43) return 10;
	FunctionReceiver from_namespace{Answers::answer};
	if (from_namespace.value != 47) return 11;
	return 0;
}
