struct Packet {
	short tag;
	int payload;
};

using VectorcallCallback = void(__vectorcall*)(Packet);
using CdeclCallback = void(__cdecl*)(Packet);

struct CallbackOwner {
	int __vectorcall invoke(Packet value) {
		return value.payload;
	}

	int __cdecl invoke_cdecl(Packet value) {
		return value.payload;
	}
};

int select_callback(VectorcallCallback) {
	return 3;
}

int select_callback(CdeclCallback) {
	return 7;
}

int select_member(decltype(&CallbackOwner::invoke)) {
	return 17;
}

int select_member(decltype(&CallbackOwner::invoke_cdecl)) {
	return 19;
}

void __vectorcall process_vectorcall(Packet value) {
	(void)value;
}

void __cdecl process_cdecl(Packet value) {
	(void)value;
}

int main() {
	VectorcallCallback vectorcall_callback = process_vectorcall;
	CdeclCallback cdecl_callback = process_cdecl;
	auto vectorcall_member = &CallbackOwner::invoke;
	auto cdecl_member = &CallbackOwner::invoke_cdecl;
	return select_callback(vectorcall_callback) +
		select_callback(cdecl_callback) +
		select_member(vectorcall_member) +
		select_member(cdecl_member) - 46;
}
