#include "BackupServer.hpp"
#include "Protocol.hpp"
#include <boost/filesystem.hpp>
#include <fstream>
#include <random>
#include <iostream>
#include <windows.h>
#include <winsock2.h>

BackupServer::BackupServer(unsigned short port, const std::string& baseDir)
    : acceptor_(io_service_, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)),
    baseDir_(baseDir)
{
}

void BackupServer::run() {
    std::cout << "Server running on port...\n";
    acceptConnections();
    io_service_.run();
}

void BackupServer::acceptConnections() {
    acceptor_.async_accept([this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
        if (!ec) {
            std::thread(&BackupServer::handleClient, this, std::move(socket)).detach();
        }
        acceptConnections(); // accept next connection
        });
}

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

            // send response header (size = 0, or maybe 1 with "OK")
            std::string response = "OK";
            std::vector<char> data(response.begin(), response.end());
            PayloadHeader ph{ static_cast<uint32_t>(data.size()) };
            ph.size = htole32(ph.size);
            boost::asio::write(socket, boost::asio::buffer(&ph, sizeof(ph)));
            boost::asio::write(socket, boost::asio::buffer(data));
            break;
        }

        case 200: {  // GET FILE
            boost::filesystem::path filePath = boost::filesystem::path(baseDir_) / userId / filename;
            

            // Check if file exists
            if (!boost::filesystem::exists(filePath)) {
                std::cerr << "File not found: " << filePath << std::endl;

                // Send error header (size 0 for example)
                PayloadHeader ph{ 0 };
                ph.size = htole32(ph.size);
                boost::asio::write(socket, boost::asio::buffer(&ph, sizeof(ph)));

                break; // exit case
            }

            // Read file data
            std::vector<char> data = getFile(userId, filename);

            // Send payload header + data
            PayloadHeader ph{ static_cast<uint32_t>(data.size()) };
            ph.size = htole32(ph.size);
            boost::asio::write(socket, boost::asio::buffer(&ph, sizeof(ph)));
            boost::asio::write(socket, boost::asio::buffer(data));

            std::cout << "Sent file: " << filename << " (" << data.size() << " bytes)" << std::endl;
            break;
        }

        case 201: {  // DELETE FILE
            deleteFile(userId, filename);
            std::cout << "Deleted file: " << filename << std::endl;
            break;
        }

        case 202: {  // LIST FILES
            std::string listFile = listFiles(userId);
            std::vector<char> data(listFile.begin(), listFile.end());
            PayloadHeader ph{ static_cast<uint32_t>(data.size()) };
            ph.size = htole32(ph.size);
            boost::asio::write(socket, boost::asio::buffer(&ph, sizeof(ph)));
            boost::asio::write(socket, boost::asio::buffer(data));
            std::cout << "Sent file list for user " << userId << std::endl;
            break;
        }

        default:
            std::cerr << "Unknown operation code: " << (int)hdr.op << std::endl;
            break;
        }
    }
    catch (std::exception& e) {
        std::cerr << "Client error: " << e.what() << std::endl;
    }
}

void BackupServer::saveFile(const std::string& userId, const std::string& filename, std::vector<char>& data) {
    boost::filesystem::path userDir = boost::filesystem::path(baseDir_) / userId;
    boost::filesystem::create_directories(userDir);
    std::ofstream out((userDir / filename).string(), std::ios::binary);
    out.write(data.data(), data.size());
}

void BackupServer::deleteFile(const std::string& userId, const std::string& filename) {
    boost::filesystem::path filePath = boost::filesystem::path(baseDir_) / userId / filename;
    if (boost::filesystem::exists(filePath))
        boost::filesystem::remove(filePath);
}

std::string BackupServer::listFiles(const std::string& userId) {
    boost::filesystem::path userDir = boost::filesystem::path(baseDir_) / userId;
    boost::filesystem::create_directories(userDir);

    std::string listFilename = generateRandomFilename(32) + ".txt";
    std::ofstream listFile((userDir / listFilename).string());
    for (auto& entry : boost::filesystem::directory_iterator(userDir)) {
        if (boost::filesystem::is_regular_file(entry.path()))
            listFile << entry.path().filename().string() << "\n";
    }
    return listFilename + "\n";
}

std::vector<char> BackupServer::getFile(const std::string& userId, const std::string& filename) {
    boost::filesystem::path filePath = boost::filesystem::path(baseDir_) / userId / filename;
    std::ifstream in(filePath.string(), std::ios::binary);
    std::vector<char> data((std::istreambuf_iterator<char>(in)), {});
    return data;
}

std::string BackupServer::generateRandomFilename(size_t length) {
    const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::default_random_engine rng(std::random_device{}());
    std::uniform_int_distribution<> dist(0, sizeof(charset) - 2);
    std::string result;
    for (size_t i = 0; i < length; ++i)
        result += charset[dist(rng)];
    return result;
}
