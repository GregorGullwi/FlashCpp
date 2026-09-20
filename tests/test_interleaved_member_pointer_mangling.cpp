// Member pointers nested inside an array pointer retain their owner and cv.
struct MemberOwner {
	int value;
	int read(double) { return value; }
};

void member_object_array(int MemberOwner::* const (*p)[3]) {}

void member_function_array(int (MemberOwner::* const (*p)[3])(double)) {}
