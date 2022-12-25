#pragma once

#include <sys/types.h>
#include <sys/wait.h>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <string>
#include <sstream>
#include <boost/asio.hpp>
#include <boost/filesystem.hpp>

using boost::asio::ip::tcp;

class Session : public std::enable_shared_from_this<Session>
{
public:
	Session(tcp::socket socket);
	
public:
	void Start();
	static void SetContext(boost::asio::io_context *io_context);

public:
	static boost::asio::io_context *io_context_;

private:
	void DoRead();
	void DoReadFromServer();
	void DoReadFromWeb();
	void DoRequestToWeb(int length);
	void DoWriteToServer(int length);
	void DoReply();
	void DoReplyReject();
	void DoReplyConnect();
	void DoReplyBind();
	void ParseSOCK4Request(int length);
	void PrintSOCK4Information(std::string S_IP, std::string S_PORT, std::string D_IP, std::string D_PORT, std::string command, std::string reply);

private:
	bool CheckFirewall(char command);

private:
	tcp::socket socket_;
	tcp::socket *web_socket;
	tcp::socket *bind_socket;
	tcp::acceptor *acceptor_;
	enum {max_length = 10240};
	char message[8];
	char data_[max_length];	
	char reply_from_web[max_length];
	char reply_from_client[max_length];

private:
	std::string DOMAIN_NAME;
	std::string D_IP;
	std::string D_PORT;

private:
	std::string status_str;
	std::string REQUEST_METHOD;
	std::string REQUEST_URI;
	std::string QUERY_STRING;
	std::string SERVER_PROTOCOL;
	std::string HTTP_HOST;
	std::string SERVER_ADDR;
	std::string SERVER_PORT;
	std::string REMOTE_ADDR;
	std::string REMOTE_PORT;
	std::string EXEC_FILE;
};

class Server
{
public:
	Server(boost::asio::io_context& io_context, short port);
private:
	void DoAccept();

private:
	tcp::acceptor acceptor_;
	std::vector<pid_t> pid_pool;
};
