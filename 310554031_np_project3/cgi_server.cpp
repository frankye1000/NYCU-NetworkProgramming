#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <string>
#include <fstream>
#include <boost/asio.hpp>
#include <boost/make_shared.hpp>
#include <boost/shared_ptr.hpp>
#include <dirent.h>

#define MAXSERVER 5
using boost::asio::ip::tcp;

using namespace std;
struct connect_server {
  string h;
  string p;
  string f;
};


//console client
class shellSession
  : public std::enable_shared_from_this<shellSession>
{
private:
  tcp::socket socket;
  boost::shared_ptr< tcp::socket > web_socket;
  tcp::resolver resolver_;
  string s, command;
  ifstream file;
  struct connect_server cs;
  enum { max_length = 1024 };
  char data_[max_length];
public:
  shellSession(boost::asio::io_context& io_context, int index, struct connect_server connect_server, boost::shared_ptr< tcp::socket > ws)
    : socket(io_context), resolver_(io_context), web_socket(ws)
  {
    s = "s"+to_string(index);
    cs = connect_server;
    file.open(".\\test_case\\"+cs.f, ifstream::in);
    
  }

  void start()
  {
    do_resolve();
  }

private:
  void do_resolve() {
    auto self(shared_from_this());
    resolver_.async_resolve(tcp::resolver::query(cs.h, cs.p), 
      [this, self](boost::system::error_code ec, tcp::resolver::iterator iterator)
      {
        if(!ec)
        {
          do_connect(iterator);
        }
      });
  }
  void do_connect(tcp::resolver::iterator iterator) 
  {
    auto self(shared_from_this());
    socket.async_connect(*iterator, 
      [this, self](boost::system::error_code ec)
      {
        do_read();
      });
  }
  void do_read()
  {
    auto self(shared_from_this());
    socket.async_read_some(boost::asio::buffer(data_, max_length),
      [this, self](boost::system::error_code ec, std::size_t length)
      {
        if (!ec)
        {  
          string data(data_, length);
          do_escape(data);
		      string script = "<script>document.getElementById('"+s+"').innerHTML += '"+data+"';</script>\n";
          boost::asio::write(*web_socket,boost::asio::buffer(script, script.length()));
          if( data.find("% ") != string::npos) 
          {
            memset(data_,0,max_length);
            do_send_cmd();
          }
          do_read();
        }
      });
  }
  void do_send_cmd() 
  {
    auto self(shared_from_this());
    command = "";
    if(getline(file, command)) 
    {
      command += "\n";
      boost::asio::async_write(socket,boost::asio::buffer(command, command.length()),
        [this, self] (boost::system::error_code ec, std::size_t /*length*/) 
        {
          if (!ec) 
          {
            do_escape(command);
			string script = "<script>document.getElementById('"+s+"').innerHTML += '<b>"+command+"</b>';</script>\n";
            boost::asio::write(*web_socket,boost::asio::buffer(script, script.length()));
          }
        }
      );
    }
  }
  void do_escape(string &s)
  {
    string ss;
    for (int i=0; i<s.length(); i++) {
      if(s[i] == '&') ss += "&amp;";
      else if(s[i] == '\"') ss += "&quot;";
      else if(s[i] == '\'') ss += "&apos;";
      else if(s[i] == '>') ss += "&gt;";
      else if(s[i] == '<') ss += "&lt;";
      else if(s[i] == '\r') ss += "";
      else if(s[i] == '\n') ss += "&NewLine;";
      else ss += s[i];
    }
    s = ss;
  }
};

//Http server
class session
  : public std::enable_shared_from_this<session>
{
public:
  session(tcp::socket socket)
    : socket_(std::move(socket)) {}
  
  void start() {
    do_read();
  }
  
private:
  void do_read() 
  {
    auto self(shared_from_this());
    socket_.async_read_some(boost::asio::buffer(request_, max_length),
      [this, self] (boost::system::error_code ec, std::size_t length) {
        if (!ec) {
          do_write();
        }
      }
    );
  }

  void do_write() 
  {
    auto self(shared_from_this());
    std::string ok = "HTTP/1.1 200 OK\n";
    boost::asio::async_write(socket_,boost::asio::buffer(ok, ok.length()),
      [this, self] (boost::system::error_code ec, std::size_t /*length*/) {
        if (!ec) {
          request_parser(request_);
          setENV();
          do_cgi();
        }
      }
    );
  }
  
  void request_parser(char request[]) 
  {
    int i=0, j=0;
    for(i=0; i<max_length; i++) 
    {
      if(request[i]!=' ') method_[j++]=request[i];
      else {method_[j]='\0';i+=2;j=0;break;} 
    }
    bool qs = false;
    for(i;i<max_length; i++) 
    {
      if(request[i]!='?' && request[i]!=' ' ) uri_[j++] = request[i];
      else {
        if(request[i]=='?') qs =true;
        uri_[j++]='\0';i++;j=0;break;
      } 
    }
    if(qs) 
    {
      for(i;i<max_length; i++) {
        if(request[i]!=' ') queryString_[j++] = request[i];
        else {queryString_[j++]='\0';i++;j=0;break;} 
      }
    }
    else queryString_[0] = '\0';
    for(i;i<max_length; i++) {
      if(request[i]!=' ') continue;
      else {i++;j=0;break;} 
    }
    bool a=false;
    int k=0;
    for(i;i<max_length; i++) {
      if(request[i]!='\r') {
        httpHost_[j++] = request[i];
        if(request[i] == ':') {
          serverAddr_[k++]='\0';
          k=0;
          a=true;
          continue;
        }
        if(!a) serverAddr_[k++] = request[i];
        else serverPort_[k++] = request[i];
      }
      else {httpHost_[j++]='\0';serverPort_[k++]='\0';i+=2;j=0;break;} 
    }
  }
  
  void setENV() 
  {
    _putenv_s("REQUEST_METHOD", method_);
    _putenv_s("REQUEST_URI", uri_);
    _putenv_s("QUERY_STRING", queryString_);
    _putenv_s("SERVER_PROTOCOL", "tcp");
    _putenv_s("HTTP_HOST", httpHost_ );
    _putenv_s("SERVER_ADDR", serverAddr_ );
    _putenv_s("SERVER_PORT", serverPort_);
    _putenv_s("REMOTE_ADDR", socket_.remote_endpoint().address().to_string().c_str());
    _putenv_s("REMOTE_PORT", std::to_string(socket_.remote_endpoint().port()).c_str());
  }
  void do_cgi() 
  {
    if( string(uri_).find("panel.cgi") != string::npos) 
    {
		  panel();
	  }
	  if(string(uri_).find("console.cgi") != string::npos) {
		  console();
	  }
  }
  
  void panel() {
		string test_case_menu = "";
		DIR *dir;
		struct dirent *ent;
		if((dir = opendir(".\\test_case\\")) != NULL) {
			while ((ent = readdir(dir)) != NULL) {
				string test_case = ent->d_name;
				if(test_case.length() >= 5) {
					test_case_menu += "<option value=\""+test_case+"\">"+test_case+"</option>\n";
				}
			}
		}
		string host_menu = "";
		for (int i=0; i<12; i++) {
			string host = "nplinux" + to_string(i+1);
			host_menu += "<option value=\"" + host + ".cs.nctu.edu.tw\">" + host + "</option>";
		}
		
		string HTML = "";
		HTML += "Content-type:text/html\r\n\r\n";
		HTML += "<!DOCTYPE html>\n";
		HTML += "<html lang=\"en\">\n";
		HTML += 	"<head>\n";
		HTML += 		"<title>NP Project 3 Panel</title>\n";
		HTML += 		"<link\n";
		HTML += 			"rel=\"stylesheet\"\n";
		HTML += 			"href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\"\n";
		HTML += 			"integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\"\n";
		HTML += 			"crossorigin=\"anonymous\"\n";
		HTML += 		"/>\n";
		HTML += 		"<link\n";
		HTML += 			"href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\"\n";
		HTML += 			"rel=\"stylesheet\"\n";
		HTML += 		"/>\n";
		HTML += 		"<link\n";
		HTML += 			"rel=\"icon\"\n";
		HTML += 			"type=\"image/png\"\n";
		HTML += 			"href=\"https://cdn4.iconfinder.com/data/icons/iconsimple-setting-time/512/dashboard-512.png\"\n";
		HTML += 		"/>\n";
		HTML += 		"<style>\n";
		HTML +=				"* {\n";
		HTML += 				"font-family: 'Source Code Pro', monospace;";
		HTML += 			"}\n";
		HTML += 		"</style>\n";
		HTML += 	"</head>\n";
		HTML += 	"<body class=\"bg-secondary pt-5\">\n";
		HTML += 		"<form action=\"console.cgi\" method=\"GET\">\n";
		HTML += 			"<table class=\"table mx-auto bg-light\" style=\"width: inherit\">\n";
		HTML += 				"<thead class=\"thead-dark\">\n";
		HTML +=						"<tr>\n";
		HTML +=							"<th scope=\"col\">#</th>\n";
		HTML +=							"<th scope=\"col\">Host</th>\n";
		HTML +=							"<th scope=\"col\">Port</th>\n";
		HTML +=							"<th scope=\"col\">Input File</th>\n";
		HTML +=						"</tr>\n";
		HTML += 				"</thead>\n";
		HTML += 				"<tbody>\n";
		for (int i=0;i<MAXSERVER;i++) {
			HTML +=					"<tr>\n";
			HTML += 					"<th scope=\"row\" class=\"align-middle\">Session" + to_string(i + 1) + "</th>\n";
			HTML += 					"<td>\n";
			HTML +=							"<div class=\"input-group\">\n";
			HTML +=								"<select name=\"h"+to_string(i)+"\" class=\"custom-select\">\n";
			HTML += 								"<option></option>" + host_menu;
			HTML += 							"</select>\n";
			HTML +=								"<div class=\"input-group-append\">";
			HTML += 								"<span class=\"input-group-text\">.cs.nctu.edu.tw</span>\n";
			HTML += 							"</div>\n";
			HTML += 						"</div>\n";
			HTML += 					"</td>\n";
			HTML += 					"<td>\n";
			HTML +=	 						"<input name=\"p"+to_string(i)+"\" type=\"text\" class=\"form-control\" size=\"5\" />\n";
			HTML += 					"</td>\n";
			HTML += 					"<td>\n";
			HTML += 						"<select name=\"f"+to_string(i)+"\" class=\"custom-select\">\n";
			HTML += 							"<option></option>\n";
			//
			HTML += 							test_case_menu;
			HTML += 						"</select>\n";
			HTML += 					"</td>\n";
			HTML +=					"</tr>\n";
		}
		HTML +=						"<tr>\n";
		HTML += 						"<td colspan=\"3\"></td>\n";
		HTML += 						"<td>\n";
		HTML += 							"<button type=\"submit\" class=\"btn btn-info btn-block\">Run</button>\n";
		HTML += 						"</td>\n";
		HTML +=						"</tr>\n";
		HTML += 				"</tbody>\n";
		HTML += 			"</table>\n";
		HTML += 		"</form>\n";
		HTML += 	"</body>\n";
		HTML += "</html>\n";
		//
		boost::asio::write(socket_,boost::asio::buffer(HTML, HTML.length()));
		//socket_.close();
	}

	void console() {
	  struct connect_server cs[MAXSERVER];
	  bool is_cs[MAXSERVER] = {false, false, false, false, false};
	  string s = getenv("QUERY_STRING");
	  char c[s.length()];
	  for(int i=0; i<s.length();i++) c[i] = s[i];
	  char* token;
	  string HTML = "";
	  HTML += "Content-type:text/html\r\n\r\n";
	  HTML += "<!DOCTYPE html>\n";
	  HTML +=   "<html lang=\"en\">\n";
	  HTML +=     "<head>\n";
	  HTML +=       "<meta charset=\"UTF-8\" />\n";
	  HTML +=       "<title>NP Project 3 Sample Console</title>\n";
	  HTML +=       "<link\n";
	  HTML +=         "rel=\"stylesheet\"\n";
	  HTML +=         "href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\"\n";
	  HTML +=         "integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\"\n";
	  HTML +=         "crossorigin=\"anonymous\"\n";
	  HTML +=       "/>\n";
	  HTML +=       "<link\n";
	  HTML +=         "href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\"\n";
	  HTML +=         "rel=\"stylesheet\"\n";
	  HTML +=       "/>\n";
	  HTML +=       "<link\n";
	  HTML +=         "rel=\"icon\"\n";
	  HTML +=         "type=\"image/png\"\n";
	  HTML +=         "https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\"\n";
	  HTML +=       "/>\n";
	  HTML +=       "<style>\n";
	  HTML +=         "* {\n";
	  HTML +=           "font-family: 'Source Code Pro', monospace;\n";
	  HTML +=           "font-size: 1rem !important;\n";
	  HTML +=         "}\n";
	  HTML +=         "body {\n";
	  HTML +=           "background-color: #212529;\n";
	  HTML +=         "}\n";
	  HTML +=         "pre {\n";
	  HTML +=           "color: #cccccc;\n";
	  HTML +=         "}\n";
	  HTML +=         "b {\n";
	  HTML +=           "color: #01b468;\n";
	  HTML +=         "}\n";
	  HTML +=       "</style>\n";
	  HTML +=     "</head>\n";
	  HTML +=     "<body>\n";
	  HTML +=       "<table class=\"table table-dark table-bordered\">\n";
	  HTML +=         "<thead>\n";
	  HTML +=           "<tr>\n";
	  token = strtok(c, "&");
	  for(int i=0; i<MAXSERVER; i++) {
		if (strlen(token) != 3) {
		  cs[i].h = string(token).substr(3).c_str();
		  is_cs[i] = true;
		}
		token = strtok(NULL, "&");
		if (strlen(token) != 3) cs[i].p= string(token).substr(3).c_str();
		token = strtok(NULL, "&");
		if (strlen(token) != 3) cs[i].f= string(token).substr(3).c_str();
		if(i != MAXSERVER-1) token = strtok(NULL, "&");
		if(is_cs[i])HTML +="<th scope=\"col\">" + cs[i].h + ":" + cs[i].p +"</th>\n";
	  }
	  
	  HTML +=           "</tr>\n";
	  HTML +=         "</thead>\n";
	  HTML +=         "<tbody>\n";
	  HTML +=           "<tr>\n";
	  for(int i=0; i<MAXSERVER; i++) {
		if(is_cs[i]) HTML += "<td><pre id=\"s"+to_string(i)+"\" class=\"mb-0\"></pre></td>\n";
	  }
	  HTML +=           "</tr>\n";
	  HTML +=         "</tbody>\n";
	  HTML +=       "</table>\n";
	  HTML +=     "</body>\n";
	  HTML +=   "</html>\n";
	  boost::shared_ptr<tcp::socket> web_socket = boost::make_shared<tcp::socket>(move(socket_));
	  boost::asio::write(*web_socket,boost::asio::buffer(HTML, HTML.length()));
	  
	  boost::asio::io_context io_context;
	  tcp::socket tcp_socket{io_context};
	  for(int i=0; i<MAXSERVER; i++) {
		if(is_cs[i]) std::make_shared<shellSession>(io_context, i, cs[i], web_socket)->start();
	  }
	  io_context.run();
	}
  tcp::socket socket_;
  enum {max_length = 1024};
  char request_[max_length], method_[max_length], uri_[max_length], queryString_[max_length], httpHost_[max_length], serverAddr_[max_length], serverPort_[max_length];
};

class http_server
{
public:
  http_server(boost::asio::io_context& io_context, short port)
    : acceptor_(io_context, tcp::endpoint(tcp::v4(), port))
  {
    do_accept();
  }
  
private:
  void do_accept() 
  {
    acceptor_.async_accept ( 
      [this] (boost::system::error_code ec, tcp::socket socket)
      {
        if (!ec) 
        {
          std::make_shared<session>(std::move(socket))->start();
        }
        do_accept();
      }
    );
  }
  tcp::acceptor acceptor_;
};

int main(int argc, char* argv[]) 
{
  try 
  {
    if (argc !=2) 
    {
      std::cerr << "Usage: async_tcp_http_server <port>\n";
      return 1;
    }
    
    boost::asio::io_context io_context;
    http_server s(io_context, std::atoi(argv[1]));
    io_context.run(); //important
  }
  catch (std::exception& e) 
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }
  return 0;
}