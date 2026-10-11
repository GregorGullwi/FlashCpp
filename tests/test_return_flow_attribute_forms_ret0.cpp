[[noreturn]] void stop_before();
void stop_before() { throw 1; }

void stop_after [[noreturn]] ();
void stop_after() { throw 2; }

void stop_after_definition [[noreturn]] () { throw 3; }

struct Stops {
	[[noreturn]] void member_leading() { throw 4; }
	void member [[noreturn]] () const { throw 5; }
	template<typename T>
	void member_template [[noreturn]] (T) const { throw 6; }
};

int free_no_return() {
	stop_after();
}

int member_no_return() {
	Stops object;
	object.member();
}

int member_template_no_return() {
	Stops object;
	object.member_template(1);
}

// Names inside vendor arguments and using-namespace blocks are not standard attributes.
[[vendor("ignored, noreturn")]] void vendor_argument() {}
[[using vendor: ignored, noreturn]] void vendor_using() {}

int ignored_attributes_can_return() {
	vendor_argument();
	vendor_using();
}

int main() {
	return 0;
}
