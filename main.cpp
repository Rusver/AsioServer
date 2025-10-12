#include "BackupServer.hpp"
#include <iostream>

/**
 * @brief this is the main where the programs starts and end
 *          the main starts the server with specified port and dir
 *          and catch error if needed
 * @return 
 */
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
