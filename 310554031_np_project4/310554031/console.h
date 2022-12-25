#pragma once

#include <vector>
#include <string>
#include <boost/asio.hpp>

class Client
{
public:
	Client(boost::asio::io_context &io_context, int port, std::string addr, std::string file);

public:
	int serverPort;
	std::string serverAddr;
	std::string testFile;
	boost::asio::ip::tcp::socket socket;
	char data_[10240];
};

class Console
{
public:
	Console();
	
public:
	void InitClients();
	void SetQuery(std::string query_string);
	void LinkToServer();
	void Run();

private:
	void SendInitialHTML();
	void SendShellInput(int session, std::vector<std::string> input);
	void SendShellOutput(int session, std::string content);
	std::vector<std::string> GetShellInput(std::string testFile);
	void GetSOCK4Reply(int session);
	void GetShellOutput(int session, std::vector<std::string> input);
	void DoWrite(std::string content);

private:
	std::string QUERY_STRING;
	std::string sh;
	std::string sp;
	std::vector<Client> clients;
	boost::asio::ip::tcp::socket *sock4_socket;
};

