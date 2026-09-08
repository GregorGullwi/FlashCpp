// Calling-convention and dll-linkage are part of canonical Function identity.
// Mixing stdcall/cdecl with a struct parameter proves the adapter no longer
// leaves those callables UnmigratedCallable.
struct Packet {
	short tag;
	int payload;
};

using StdcallSink = void(__stdcall*)(Packet);
using CdeclSink = void(__cdecl*)(Packet);

void __stdcall take_stdcall(Packet value) { (void)value; }
void __cdecl take_cdecl(Packet value) { (void)value; }

int main() {
	StdcallSink stdcall_sink = take_stdcall;
	CdeclSink cdecl_sink = take_cdecl;
	Packet packet{3, 5};
	stdcall_sink(packet);
	cdecl_sink(packet);
	return packet.tag + packet.payload - 8;
}
