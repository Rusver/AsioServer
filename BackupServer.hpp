#pragma once
#include <boost/asio.hpp>
#include <string>
#include <thread>
#include <vector>

class BackupServer {
public:
    BackupServer(unsigned short port, const std::string& baseDir);
    void run();

private:
    void acceptConnections();
    void handleClient(boost::asio::ip::tcp::socket socket);

    void saveFile(const std::string& userId, const std::string& filename, std::vector<char>& data);
    void deleteFile(const std::string& userId, const std::string& filename);
    std::string listFiles(const std::string& userId);
    std::vector<char> getFile(const std::string& userId, const std::string& filename);

    std::string generateRandomFilename(size_t length);

    boost::asio::io_context io_service_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::string baseDir_;
};
