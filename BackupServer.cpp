#include "BackupServer.hpp"
#include "Protocol.hpp"
#include <boost/filesystem.hpp>
#include <fstream>
#include <random>
#include <iostream>
#include <windows.h>
#include <winsock2.h>

/// Send response for the client by the protocol
 /**
  *  if its size is 0 - the client will understand there is an error or empty file
  *  for example a file of empty list dir without names inside.. (0 bytes)
  *
  */
void sendResponse(boost::asio::ip::tcp::socket& socket, uint8_t version, uint16_t status, const std::string& message = "") {
    ResponseHeader rh{ version, static_cast<uint16_t>(htole16(status)) };
    boost::asio::write(socket, boost::asio::buffer(&rh, sizeof(rh)));

    if (!message.empty()) {
        std::vector<char> data(message.begin(), message.end());
        PayloadHeader ph{ static_cast<uint32_t>(data.size()) };
        ph.size = htole32(ph.size);
        boost::asio::write(socket, boost::asio::buffer(&ph, sizeof(ph)));
        boost::asio::write(socket, boost::asio::buffer(data));
    }
}


/// Constructor for the Server
 /**
  *
  * @throws boost::system::system_error Thrown on failure.
  *
  */

BackupServer::BackupServer(unsigned short port, const std::string& baseDir)
    : acceptor_(io_service_, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)),
    baseDir_(baseDir)
{
}

/// make the Server run..
 /**
  *
  * @prints that the server is running and the port number..
  *
  */

void BackupServer::run() {
    unsigned int port = acceptor_.local_endpoint().port();
    std::cout << "Server running on port " << port << "..." << std::endl;
    acceptConnections();
    io_service_.run();
}

// make the Server stateless accept connections (asynchronyc)


void BackupServer::acceptConnections() {
    acceptor_.async_accept([this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
        if (!ec) {
            std::thread(&BackupServer::handleClient, this, std::move(socket)).detach();
        }
        acceptConnections(); // accept next connection
        });
}

// Handle the connections to the server and handle their requests by the protocol


void BackupServer::handleClient(boost::asio::ip::tcp::socket socket) {
    try {
        RequestHeader hdr;
        boost::asio::read(socket, boost::asio::buffer(&hdr, sizeof(hdr)));

        // Convert little-endian fields
        hdr.user_id = le32toh(hdr.user_id);
        hdr.name_len = le16toh(hdr.name_len);

        // Read filename
        std::vector<char> nameBuf(hdr.name_len);
        boost::asio::read(socket, boost::asio::buffer(nameBuf.data(), hdr.name_len));
        std::string filename(nameBuf.begin(), nameBuf.end());

        std::cout << "User " << hdr.user_id
            << " requested op=" << (int)hdr.op
            << " file=" << filename
            << " (ver " << (int)hdr.version << ")" << std::endl;

        // Base directory per user
        std::string userId = std::to_string(hdr.user_id);

        // === Handle by op code ===
        switch (hdr.op) {
        case 100: {  // SAVE FILE
            PayloadHeader ph;
            boost::asio::read(socket, boost::asio::buffer(&ph, sizeof(ph)));
            ph.size = le32toh(ph.size);

            std::vector<char> fileData(ph.size);
            boost::asio::read(socket, boost::asio::buffer(fileData.data(), ph.size));

            saveFile(userId, filename, fileData);
            std::cout << "Saved file: " << filename << " (" << fileData.size() << " bytes)" << std::endl;

            sendResponse(socket, hdr.version, 212, "File saved");
            break;
        }

        case 200: {  // GET FILE
            boost::filesystem::path filePath = boost::filesystem::path(baseDir_) / userId / filename;
            

            // Check if file exists
            if (!boost::filesystem::exists(filePath)) {
                std::cerr << "File not found: " << filePath << std::endl;
                sendResponse(socket, hdr.version, 1001); // file doesn't exist

                break; // exit case
            }

            // Read file data
            std::vector<char> data = getFile(userId, filename);

            // Send response header + data
            sendResponse(socket, hdr.version, 210, std::string(data.begin(), data.end()));

            std::cout << "Sent file: " << filename << " (" << data.size() << " bytes)" << std::endl;
            
            break;
        }

        case 201: {  // DELETE FILE
            deleteFile(userId, filename);
            std::cout << "Deleted file: " << filename << std::endl;

            sendResponse(socket, hdr.version, 212, "File deleted");
            break;
        }

        case 202: {  // LIST FILES
            std::string listFile = listFiles(userId);

            if (listFile.empty()) {
                sendResponse(socket, hdr.version, 1002);  // no files on the server
                std::cerr << "No files found for user " << userId << std::endl;
                break;
            }

            sendResponse(socket, hdr.version, 211, std::string(listFile.begin(), listFile.end())); // success: list file generated and sent
            std::cout << "Sent list file name: " << listFile << std::endl;
            break;
        }

        default:
            sendResponse(socket, hdr.version, 1003); // general server error
            std::cerr << "Unknown op: " << (int)hdr.op << std::endl;
            break;
        }
    }
    catch (std::exception& e) {
        std::cerr << "Client error: " << e.what() << std::endl;
    }
}

// This function gets the file via the data parameter and saves it to a file via the filename parm
void BackupServer::saveFile(const std::string& userId, const std::string& filename, std::vector<char>& data) {
    boost::filesystem::path userDir = boost::filesystem::path(baseDir_) / userId;
    boost::filesystem::create_directories(userDir);
    std::ofstream out((userDir / filename).string(), std::ios::binary);
    out.write(data.data(), data.size());
}


// This function delets the specified file via the filename parm
void BackupServer::deleteFile(const std::string& userId, const std::string& filename) {
    boost::filesystem::path filePath = boost::filesystem::path(baseDir_) / userId / filename;
    if (boost::filesystem::exists(filePath))
        boost::filesystem::remove(filePath);
}

//This function generates the list file as mentioned in the maman  
/*
*   the function generates random filename 32 length made of 'A-Za-z0-9' combination
*   if there is no files for the user return empty String and the Handle function will handle it
*   the function check what are the files inside the diretory and writes it to the file
*   @return the list filename that the function generated
*/
std::string BackupServer::listFiles(const std::string& userId) {
    boost::filesystem::path userDir = boost::filesystem::path(baseDir_) / userId;
    if (!boost::filesystem::exists(userDir) || boost::filesystem::is_empty(userDir)) {
        return ""; // no files -> let handleClient send error 1002
    }

    // Generate random filename
    std::string listFilename = generateRandomFilename(32) + ".txt";
    boost::filesystem::path listFilePath = userDir / listFilename;

    std::ofstream listFile(listFilePath.string());
    bool hasFiles = false;

    // Write only actual files
    for (auto& entry : boost::filesystem::directory_iterator(userDir)) {
        if (boost::filesystem::is_regular_file(entry.path()) &&
            entry.path().filename() != listFilePath.filename()) {
            listFile << entry.path().filename().string() << "\n";
            hasFiles = true;
        }
    }

    listFile.close();

    // If no files were written -> delete the file and return empty
    if (!hasFiles) {
        boost::filesystem::remove(listFilePath);
        return "";
    }

    return listFilename;
}

/**
 * @brief returns the file asked via the filename
 * @param userId 
 * @param filename 
 * @return 
 */
std::vector<char> BackupServer::getFile(const std::string& userId, const std::string& filename) {
    boost::filesystem::path filePath = boost::filesystem::path(baseDir_) / userId / filename;
    std::ifstream in(filePath.string(), std::ios::binary);
    std::vector<char> data((std::istreambuf_iterator<char>(in)), {});
    return data;
}

/**
 * @brief generating the filename as requested, randomly.. 
 * @param length 
 * @return 
 */
std::string BackupServer::generateRandomFilename(size_t length) {
    const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::default_random_engine rng(std::random_device{}());
    std::uniform_int_distribution<> dist(0, sizeof(charset) - 2);
    std::string result;
    for (size_t i = 0; i < length; ++i)
        result += charset[dist(rng)];
    return result;
}



