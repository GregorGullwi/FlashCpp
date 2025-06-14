#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include <cstring>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " <obj_file>" << std::endl;
        return 1;
    }
    
    std::ifstream file(argv[1], std::ios::binary);
    if (!file) {
        std::cout << "Error: Cannot open file " << argv[1] << std::endl;
        return 1;
    }
    
    // Read the entire file
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);
    
    std::cout << "File size: " << size << " bytes" << std::endl;
    
    // Look for debug sections by searching for ".debug$S" and ".debug$T"
    std::string debug_s = ".debug$S";
    std::string debug_t = ".debug$T";
    
    for (size_t i = 0; i < size - debug_s.length(); ++i) {
        if (memcmp(data.data() + i, debug_s.c_str(), debug_s.length()) == 0) {
            std::cout << "Found .debug$S section at offset: 0x" << std::hex << i << std::dec << std::endl;
        }
        if (memcmp(data.data() + i, debug_t.c_str(), debug_t.length()) == 0) {
            std::cout << "Found .debug$T section at offset: 0x" << std::hex << i << std::dec << std::endl;
        }
    }
    
    return 0;
}
