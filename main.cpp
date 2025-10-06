#include "BackupServer.hpp"
#include <iostream>

int main() {
    try {
        BackupServer server(8080, "C:\\backupsvr");
        server.run();
    }
    catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
