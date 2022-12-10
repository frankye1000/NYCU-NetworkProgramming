#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <string>
#include <boost/asio.hpp>
#include<sys/wait.h>
#include<sys/errno.h>
using boost::asio::ip::tcp;
extern int errno;
class session
  : public std::enable_shared_from_this<session>
{
public:
  session(tcp::socket socket)
    : socket_(std::move(socket)) {}
  
  void start() 
  {
    do_read();
  }
  
private:
  void do_read() 
  {
    auto self(shared_from_this());
    socket_.async_read_some(boost::asio::buffer(request_, max_length),
      [this, self] (boost::system::error_code ec, std::size_t length) 
      {
        if (!ec) 
        {
          do_write();
        }
      }
    );
  }

  /* 
  1. request parser
  2. set env
  3. do cgi
  */
  void do_write() 
  {
    auto self(shared_from_this());
    std::string ok = "HTTP/1.1 200 OK\n";
    boost::asio::async_write(socket_,boost::asio::buffer(ok, ok.length()),
      [this, self] (boost::system::error_code ec, std::size_t /*length*/) 
      {
        if (!ec) 
        {
          request_parser(request_);
          setENV();
          do_cgi();
        }
      }
    );
  }

  /*
  Ex:
  GET /console.cgi?h0=nplinux1.cs.nctu.edu.tw&p0= ...
  Host: nplinux8.cs.nctu.edu.tw:7779
  */
  void request_parser(char request[]) 
  {   
    int i=0, j=0;
    for(i=0; i<max_length; i++)                  // method:GET 
    {
      if(request[i]!=' ') method_[j++]=request[i];
      else {method_[j]='\0';i+=2;j=0;break;}    
    }

    bool qs = false;
    for(;i<max_length; i++)                     // uri:console.cgi
    {  
      if(request[i]!='?' && request[i]!=' ' ) uri_[j++] = request[i];
      else {
        if(request[i]=='?') qs = true;
        uri_[j++]='\0';i++;j=0;break;          
      } 
    }

    if(qs) {                                     // querystring:h0=nplinux1.cs.nctu.edu.tw&p0= ...
      for(;i<max_length; i++) 
      {
        if(request[i]!=' ') queryString_[j++] = request[i];
        else {queryString_[j++]='\0';i++;j=0;break;} 
      }
    }
    else queryString_[0] = '\0';

    for(;i<max_length; i++) 
    {
      if(request[i]!=' ') continue;
      else {i++;j=0;break;} 
    }

    bool a=false;
    int k=0;
    for(;i<max_length; i++) 
    {
      if(request[i]!='\r')                      // httphost:nplinux8.cs.nctu.edu.tw:7779
      {
        httpHost_[j++] = request[i];
        if(request[i] == ':') 
        {
          serverAddr_[k++]='\0';
          k=0;
          a=true;
          continue;
        }
        if(!a) serverAddr_[k++] = request[i];   // serverAddr:nplinux8.cs.nctu.edu.tw
        else serverPort_[k++] = request[i];     // serverPort:7779
      }
      else {httpHost_[j++]='\0';serverPort_[k++]='\0';i+=2;j=0;break;} 
    }
  }
  
  void setENV() 
  { 
    setenv("REQUEST_METHOD", method_ ,1);
    setenv("REQUEST_URI", uri_ ,1);
    setenv("QUERY_STRING", queryString_ ,1);   // for console.cgi get
    setenv("SERVER_PROTOCOL", "tcp" ,1);
    setenv("HTTP_HOST", httpHost_ ,1);
    setenv("SERVER_ADDR", serverAddr_ ,1);
    setenv("SERVER_PORT", serverPort_ ,1);
    setenv("REMOTE_ADDR", socket_.remote_endpoint().address().to_string().c_str() ,1);
    setenv("REMOTE_PORT", std::to_string(socket_.remote_endpoint().port()).c_str() ,1);
    
    std::cout<<"REQUEST_METHOD  :"<<getenv("REQUEST_METHOD")<<std::endl;
    std::cout<<"REQUEST_URI     :"<<getenv("REQUEST_URI")<<std::endl;
    std::cout<<"QUERY_STRING    :"<<getenv("QUERY_STRING")<<std::endl;
    std::cout<<"SERVER_PROTOCOL :"<<getenv("SERVER_PROTOCOL")<<std::endl;
    std::cout<<"HTTP_HOST       :"<<getenv("HTTP_HOST")<<std::endl;
    std::cout<<"SERVER_ADDR     :"<<getenv("SERVER_ADDR")<<std::endl;
    std::cout<<"SERVER_PORT     :"<<getenv("SERVER_PORT")<<std::endl;
    std::cout<<"REMOTE_ADDR     :"<<getenv("REMOTE_ADDR")<<std::endl;
    std::cout<<"REMOTE_PORT     :"<<getenv("REMOTE_PORT")<<std::endl;
  }
  
  void do_cgi() 
  {
    pid_t pid;
    int wstatus;
    while ((pid = fork()) < 0) waitpid(-1, &wstatus, 0);
    if (pid == 0)
    {
      char* c[2];
      std::strcpy(c[0], std::string(uri_).c_str());   // uri_:panel.cgi || console.cgi
      c[1] = NULL;
      int fd = socket_.native_handle();
      dup2(fd,STDIN_FILENO);
      dup2(fd,STDOUT_FILENO);
      socket_.close();
      execv(c[0], c);    // exec cgi
      std::cerr << std::strerror(errno) << std::endl;
      exit(0);
    }
    else
    {
      socket_.close();
    }
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
    acceptor_.async_accept 
    ( 
      [this] (boost::system::error_code ec, tcp::socket socket)
      {
        if (!ec) 
        {
          std::make_shared<session>(std::move(socket))->start();   // start !!
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