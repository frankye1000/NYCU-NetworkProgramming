#include "console.h"

#include <cstdio>
#include <regex>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <string>
#include <sstream>
#include <fstream>

boost::asio::io_context global_io_context;

Client::Client(boost::asio::io_context &io_context, int port, std::string addr, std::string file)
	: serverPort(port), serverAddr(addr), testFile(file), socket(io_context)
{

}

Console::Console()
{

}

void Console::InitClients()
{
	QUERY_STRING = getenv("QUERY_STRING");

	std::string buffer(QUERY_STRING);
	std::string temp, port, addr, file;

	std::replace(buffer.begin(), buffer.end(), '&', ' ');
	std::istringstream iss(buffer);

	if (buffer.length() == 0) return;

	for (int i = 0; i < 5; ++i)
	{
		iss >> temp;
		if (temp.length() != 0) addr = temp.substr(3, temp.length() - 1);
		iss >> temp;
		if (temp.length() != 0) port = temp.substr(3, temp.length() - 1);
		iss >> temp;
		if (temp.length() != 0) file = temp.substr(3, temp.length() - 1);

		if (addr.length() != 0 && port.length() != 0 && file.length() != 0)
		{
			clients.push_back(Client(global_io_context, stoi(port), addr, file));
		}
	}
	// ...&sh=nplinux3.cs.nctu.edu.tw&sp=12345
	iss >> temp;
	sh = temp.substr(3, temp.length() - 1);
	iss >> temp;
	sp = temp.substr(3, temp.length() - 1);	
}

void Console::SetQuery(std::string query_string)
{
	QUERY_STRING = query_string;
}

void Console::LinkToServer()
{
	for (int i = 0; i < (int)clients.size(); ++i)
	{
		std::string sock_request = "";

		sock_request += (char)4;
		sock_request += (char)1;

		int temp = clients[i].serverPort / 256;
		sock_request += (char)(temp > 128 ? temp - 256 : temp);
		temp = clients[i].serverPort % 256;
		sock_request += (char)(temp > 128 ? temp - 256 : temp);

		sock_request += (char)0;
		sock_request += (char)0;
		sock_request += (char)0;
		sock_request += (char)1;

		sock_request += (char)0;

		sock_request += clients[i].serverAddr;
		sock_request += (char)0;
	
		boost::asio::ip::tcp::resolver resolver(global_io_context);
		boost::asio::ip::tcp::resolver::query query(sh, sp);
		boost::asio::ip::tcp::resolver::iterator iter = resolver.resolve(query);
		boost::asio::ip::tcp::endpoint endpoint = *iter;

		clients[i].socket.connect(endpoint);
		
		boost::asio::async_write(clients[i].socket, boost::asio::buffer(sock_request),
			[this, i](boost::system::error_code ec, std::size_t len)
			{
				if (ec) return;

				GetSOCK4Reply(i);
			});
	}
}

void Console::Run()
{
	SendInitialHTML();

	global_io_context.run();
}

void Console::SendInitialHTML()
{
	std::string contentType;
	std::string initHTML;

	contentType = "Content-type: text/html\r\n\r\n";
	initHTML =
		"<!DOCTYPE html>\
		<html lang=\"en\">\
  			<head>\
    			<meta charset=\"UTF-8\" />\
    			<title>NP Project 3 Sample Console</title>\
    			<link\
      				rel=\"stylesheet\"\
      				href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\"\
      				integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\"\
      				crossorigin=\"anonymous\"\
    			/>\
    			<link\
      				href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\"\
      				rel=\"stylesheet\"\
    			/>\
    			<link\
      				rel=\"icon\"\
      				type=\"image/png\"\
      				href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\"\
    			/>\
    			<style>\
      				* {\
        				font-family: 'Source Code Pro', monospace;\
        				font-size: 1rem !important;\
      				}\
      				body {\
        				background-color: #212529;\
      				}\
      				pre {\
        				color: #cccccc;\
      				}\
      				b {\
        				color: #01b468;\
      				}\
    			</style>\
  			</head>\
  			<body>\
    			<table class=\"table table-dark table-bordered\">\
      				<thead>\
        				<tr>";
	for (int i = 0; i < (int)clients.size(); ++i)
	{
		initHTML += "<th scope=\"col\">" + clients[i].serverAddr + ":" + std::to_string(clients[i].serverPort) + "</th>";
	}

	initHTML += 	        	"</tr>\
      				</thead>\
      				<tbody>\
        				<tr>";
	for (int i = 0; i < (int)clients.size(); ++i)
	{
		initHTML += "<td><pre id = \"s" + std::to_string(i) + "\" class=\"mb-0\"></pre></td>";
	}

	initHTML += 			"</tr>\
      				</tbody>\
    			</table>\
  			</body>\
		</html>";

	DoWrite(contentType);
	DoWrite(initHTML);
}

void Console::SendShellInput(int session, std::vector<std::string> input)
{
	if (input.size() == 0) return;

	boost::asio::async_write(clients[session].socket, boost::asio::buffer(input[0], input[0].length()),
		[this, input, session](boost::system::error_code ec, std::size_t )
		{
			std::string from[7] = {"&", "\"", "\'", "<", ">", "\n", "\r\n"};
			std::string to[7] = {"&amp;", "&quot", "&#39;", "&lt;", "&gt;", "&NewLine;", "&NewLine;"};
			std::string content = input[0];
			size_t pos = 0;

			for (int i = 0; i < 7; ++i)
			{
				pos = content.find(from[i], pos);

				while (pos != std::string::npos)
				{
					content.replace(pos, from[i].length(), to[i]);
					pos += to[i].length();
					pos = content.find(from[i], pos);
				}
	
				pos = 0;
			}

			std::string buffer = "<script>document.getElementById(\'s" + std::to_string(session) + "\').innerHTML += \'<b>" + content + "</b>\';</script>";
			DoWrite(buffer);

			std::vector<std::string> temp = input;

			temp.erase(temp.begin(), temp.begin() + 1);			

			GetShellOutput(session, temp);
		});
}

void Console::SendShellOutput(int session, std::string content)
{
	std::string from[7] = {"&", "\"", "\'", "<", ">", "\n", "\r\n"};
	std::string to[7] = {"&amp;", "&quot", "&#39;", "&lt;", "&gt;", "&NewLine;", "&NewLine;"};
	size_t pos = 0;
	
	for (int i = 0; i < 6; ++i)
	{
		pos = content.find(from[i], pos);

		while (pos != std::string::npos)
		{

			content.replace(pos, from[i].length(), to[i]);
			pos += to[i].length();
			pos = content.find(from[i], pos);
		}

		pos = 0;
	}

	std::string temp = "<script>document.getElementById(\'s" + std::to_string(session) + "\').innerHTML += \'" + content + "\';</script>";
	DoWrite(temp);
}

void Console::GetSOCK4Reply(int session)
{
	clients[session].socket.async_read_some((boost::asio::buffer(clients[session].data_, 8)),
		[this, session](boost::system::error_code ec, std::size_t length)
		{
			if (ec || clients[session].data_[0] != 0) 
			{
				clients[session].socket.close();
				return;
			}

			std::vector<std::string> input = GetShellInput(clients[session].testFile);
			GetShellOutput(session, input);
		});
}

std::vector<std::string> Console::GetShellInput(std::string testFile)
{
	std::ifstream in("./test_case/" + testFile);
	char buffer[10240];
	std::vector<std::string> res;

	memset(buffer, 0, 10240);

	if (in.is_open())
	{	
		while(in.getline(buffer, 10240))
		{
			res.push_back(std::string(buffer) + "\n");
			memset(buffer, 0, 10240);
		}	
	}

	return res;
}

void Console::GetShellOutput(int session, std::vector<std::string> input)
{
	clients[session].socket.async_read_some(boost::asio::buffer(clients[session].data_, 10240),
		[this, input, session](boost::system::error_code ec, std::size_t length)
		{
			if (ec)	
			{
				if (ec == boost::asio::error::eof)
				{
					clients[session].socket.close();
				}

				return;
			}

			std::string temp = "";
			for (int i = 0; i < (int)length; ++i) temp += clients[session].data_[i];

			memset(clients[session].data_, 0, 10240);

			SendShellOutput(session, temp);

			if (temp.find("%") == std::string::npos) 
			{
				GetShellOutput(session, input);
			}
			else
			{  // send to np_single_golden
				SendShellInput(session, input);
			}
		});
}

void Console::DoWrite(std::string content)
{
	std::cout << content;
	fflush(stdout);
}

int main(int argc, char* argv[])
{
	Console myConsole;

	myConsole.InitClients();

	myConsole.Link2Server();

	myConsole.Run();

	return 0;
}
