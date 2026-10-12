// Taking the address of a virtual member function is not representable by the
// current member-pointer model, so it is rejected with
// VirtualMemberFunctionAddressUnsupported rather than the generic string.

struct VirtualBase {
	virtual void run();
};

int main() {
	auto pointer = &VirtualBase::run;
	(void)pointer;
	return 0;
}
