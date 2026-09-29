struct Packet {
	short tag;
	int payload;
};

using VectorcallCallback = void(__vectorcall*)(Packet);
using CdeclCallback = void(__cdecl*)(Packet);

int select_callback(VectorcallCallback) {
	return 3;
}

int select_callback(CdeclCallback) {
	return 7;
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
	return select_callback(vectorcall_callback) +
		select_callback(cdecl_callback) - 10;
}
