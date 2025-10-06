#include "BackupServer.hpp"
#include <boost/filesystem.hpp>
#include <fstream>
#include <random>
#include <iostream>
#define _WIN32_WINNT 0x0601
#include <windows.h>

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
        boost::asio::streambuf buf;
        boost::asio::read_until(socket, buf, "\n"); // protocol: first line = command
        std::istream is(&buf);
        std::string command;
        std::getline(is, command);

        // simple protocol: CMD USERID FILENAME
        std::string userId, filename;
        is >> userId >> filename;

        if (command == "SAVE") {
            std::vector<char> fileData((std::istreambuf_iterator<char>(is)), {});
            saveFile(userId, filename, fileData);
            boost::asio::write(socket, boost::asio::buffer("OK\n"));
        }
        else if (command == "DELETE") {
            deleteFile(userId, filename);
            boost::asio::write(socket, boost::asio::buffer("OK\n"));
        }
        else if (command == "LIST") {
            std::string listFile = listFiles(userId);
            boost::asio::write(socket, boost::asio::buffer(listFile));
        }
        else if (command == "GET") {
            std::vector<char> data = getFile(userId, filename);
            boost::asio::write(socket, boost::asio::buffer(data));
        }
        else {
            boost::asio::write(socket, boost::asio::buffer("ERROR Unknown command\n"));
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
