#include <cstdlib>
#include <string>
#include <iostream>
#include <memory>
#include <utility>
#include <fstream>
#include <boost/asio.hpp>

#define MAXSERVER 5

using namespace std;
struct connect_server 
{
  string h; // host
  string p; // port
  string f; // filename
};

using boost::asio::ip::tcp;

class shellSession
  : public std::enable_shared_from_this<shellSession>
{
private:
  tcp::socket socket_;
  tcp::resolver resolver_;   // 解析host 
  string s, command;
  ifstream file;
  struct connect_server cs;
  enum { max_length = 1024 };
  char data_[max_length];
public:
  shellSession(boost::asio::io_context& io_context, int index, struct connect_server connect_server)
    : socket_(io_context), resolver_(io_context)
  {
    s = "s" + to_string(index);
    cs = connect_server;
    file.open("./test_case/" + cs.f, ifstream::in);   // read cmd file
  }

  void start()
  {
    do_resolve();
  }

private:
  void do_resolve() 
  {
    auto self(shared_from_this());
    resolver_.async_resolve(tcp::resolver::query(cs.h, cs.p), // check host&port
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
    socket_.async_connect(*iterator, 
      [this, self](boost::system::error_code ec)
      {
        // do_read();
        if(!ec)
        {
          do_read();
        }
      });
  }
  void do_read()
  {
    auto self(shared_from_this());
    socket_.async_read_some(boost::asio::buffer(data_, max_length),
      [this, self](boost::system::error_code ec, std::size_t length)
      {
        if (!ec)
        {  
          string data(data_, length);
          do_escape(data);   // turn to html type !!!!!!!!!!
          cout <<"<script>document.getElementById('"<<s<<"').innerHTML += '"<<data<<"';</script>\n";
          if( data.find("% ") != string::npos) // send cmd to np_single_golden
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
      boost::asio::async_write(socket_,boost::asio::buffer(command, command.length()),
        [this, self] (boost::system::error_code ec, std::size_t /*length*/) 
        {
          if (!ec) 
          {
            do_escape(command);
            cout <<"<script>document.getElementById('"<<s<<"').innerHTML += '<b>"<<command<<"</b>';</script>\n";
          }
        }
      );
    }
  }
  void do_escape(string &s)   // html
  {
    string ss;
    for (unsigned int i=0; i<s.length(); i++) 
    {
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


void partial_html_template()
{
  cout << "Content-type:text/html\r\n\r\n";
  cout << "<!DOCTYPE html>\n";
  cout <<   "<html lang=\"en\">\n";
  cout <<     "<head>\n";
  cout <<       "<meta charset=\"UTF-8\" />\n";
  cout <<       "<title>NP Project 3 Sample Console</title>\n";
  cout <<       "<link\n";
  cout <<         "rel=\"stylesheet\"\n";
  cout <<         "href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\"\n";
  cout <<         "integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\"\n";
  cout <<         "crossorigin=\"anonymous\"\n";
  cout <<       "/>\n";
  cout <<       "<link\n";
  cout <<         "href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\"\n";
  cout <<         "rel=\"stylesheet\"\n";
  cout <<       "/>\n";
  cout <<       "<link\n";
  cout <<         "rel=\"icon\"\n";
  cout <<         "type=\"image/png\"\n";
  cout <<         "https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\"\n";
  cout <<       "/>\n";
  cout <<       "<style>\n";
  cout <<         "* {\n";
  cout <<           "font-family: 'Source Code Pro', monospace;\n";
  cout <<           "font-size: 1rem !important;\n";
  cout <<         "}\n";
  cout <<         "body {\n";
  cout <<           "background-color: #212529;\n";
  cout <<         "}\n";
  cout <<         "pre {\n";
  cout <<           "color: #cccccc;\n";
  cout <<         "}\n";
  cout <<         "b {\n";
  cout <<           "color: #01b468;\n";
  cout <<         "}\n";
  cout <<       "</style>\n";
  cout <<     "</head>\n";
  cout <<     "<body>\n";
  cout <<       "<table class=\"table table-dark table-bordered\">\n";
  cout <<         "<thead>\n";
  cout <<           "<tr>\n";
}


int main ()
{
  struct connect_server cs[MAXSERVER];
  bool is_cs[MAXSERVER] = {false, false, false, false, false};  // open which console
  string s = getenv("QUERY_STRING");                            // get from http server
  char *c = new char[s.length()];
  // char c[s.length()];
  for(unsigned int i=0; i<s.length();i++) c[i] = s[i];
  
  partial_html_template(); // head partial html template

  /*
  parse QUERY_STRING
  Ex:
  h0=nplinux2.cs.nctu.edu.tw&
  p0=11111&
  f0=t1.txt&
  h1=nplinux2.cs.nctu.edu.tw&
  p1=11111&...
  */

  char* token;
  token = strtok(c, "&");      
  for(int i=0; i<MAXSERVER; i++) 
  {
    if (strlen(token) != 3) 
    {
      cs[i].h = string(token).substr(3).c_str();
      is_cs[i] = true;
    }
    token = strtok(NULL, "&");
    if (strlen(token) != 3) cs[i].p= string(token).substr(3).c_str();
    token = strtok(NULL, "&");
    if (strlen(token) != 3) cs[i].f= string(token).substr(3).c_str();
    if(i != MAXSERVER-1) token = strtok(NULL, "&");
    if(is_cs[i])cout<<"<th scope=\"col\">" <<cs[i].h << ":" << cs[i].p <<"</th>\n";
  }
  
  cout <<           "</tr>\n";
  cout <<         "</thead>\n";
  cout <<         "<tbody>\n";
  cout <<           "<tr>\n";
  for(int i=0; i<MAXSERVER; i++) 
  {
    if(is_cs[i]) cout << "<td><pre id=\"s"<<to_string(i)<<"\" class=\"mb-0\"></pre></td>\n";   // column index: s1,s1,s2,s3,s4
  }
  cout <<           "</tr>\n";
  cout <<         "</tbody>\n";
  cout <<       "</table>\n";
  cout <<     "</body>\n";
  cout <<   "</html>\n";
  
  boost::asio::io_context io_context;
  tcp::socket tcp_socket{io_context};
  for(int i=0; i<MAXSERVER; i++) 
  {
    if(is_cs[i]) make_shared<shellSession>(io_context, i, cs[i])->start();
  }
  io_context.run();
  return 0;
}