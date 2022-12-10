/* version 2, 可過三個測資 */
#include<iostream>
#include<fstream>
#include<sstream>
#include<vector>
using namespace std;

#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<sys/wait.h>
#include<sys/types.h>
#include<sys/signal.h>
#include<sys/socket.h>
#include<sys/errno.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<netdb.h>
#include<fcntl.h>

#define FLAGS O_RDWR|O_CREAT|O_TRUNC 
#define mode 0666 
#define MAXNUMBERPIPE 2000
#define PIPEFDN 500
#define QLEN 30 /* maximux connection queue.*/

const char* builtIn_command[3] = {"setenv", "printenv", "exit" };
extern int errno;
u_short portbase = 0;

void reaper(int);
int TCPechod(int fd);
int shell(int client_fd);
int readline(int fd, char *ptr, int maxlen);
int passiveTCP(const char *service, int qlen);
vector<string> split_cmdline_by_numpipe(string cmdline);
bool replace(string& str, const string& from, const string& to);
int passivesock(const char *service, char *protocol, int qlen); // 建立socket連線
//shell init
int NumPipeQueue[MAXNUMBERPIPE][3] = {0}, head = 0, tail = 0;   

int main(int argc, char *argv[]) {
    char *service = argv[0]; /* service name or port number */
    struct sockaddr_in fsin; /* client address */
    socklen_t AddrLen; // length of client addr 
    int msock, ssock;
    
    switch (argc) 
    {
        case 1:
            break;
        case 2:
            service = argv[1];
            break;
        default:
            cerr << "usage: TCPechod [port]\n";
            exit(EXIT_SUCCESS);
    }
    
    msock = passiveTCP(service, QLEN);   // master socket

    signal(SIGCHLD, reaper);  // handle zombie process
    
    while(1) 
    {
      AddrLen = sizeof(fsin);
      ssock   = accept(msock, (struct sockaddr *)&fsin, &AddrLen);   // slaver
      if (ssock < 0) 
      {
        if (errno == EINTR) continue;
        cerr << "accept: " << strerror(errno) << endl;
        exit(EXIT_SUCCESS);
      }
      
      switch(fork()) 
      {
        case 0: //child
          close(msock);  // close master socket
          exit(TCPechod(ssock));
        default: //parent
          close(ssock);  // close slaver socket
          break;
        case -1:
          cerr << "fork: " << strerror(errno) << endl;
          exit(EXIT_SUCCESS);
      }
    }
    return 0;
}

int shell(int client_fd) {
  int cmdlineLength;  // 輸入長度
  int wstatus;
  char input[15001]="";
  //read command
  cmdlineLength = readline(client_fd, input, sizeof input);
  if(cmdlineLength < 0){
    cerr << "echo read: " << strerror(errno) << endl;
    exit(EXIT_FAILURE);
  }
  else if (cmdlineLength == 1) {    // 輸入enter
    return 1;
  }  

  // 最一開始就應該依照number pipe切割
  string cmdline = input;
  replace(cmdline, "\n", "");
  vector<string> S_C_B_N = split_cmdline_by_numpipe(cmdline+" ");//最後面加NULL
  
  char ARR[cmdline.length() + 1]; 
  for(int v=0; v<S_C_B_N.size(); v++){
    strcpy(ARR, S_C_B_N[v].c_str()); 
    // cout<<"["<<ARR<<"]"<<endl;

    char *commands[2000][10], *token, space[2]=" ";
    int execType = 1, indexL = 0, indexP = 0;
    bool is_jump = false, write2file = false;
    int NoUseNumPipe[PIPEFDN];
    int read_len = 0;
    char buf[1024] = {0};

    /* parse the commands */
    token = strtok(ARR, space);
    while(token != NULL) 
    {
      if(strcmp(token,"|")==0 || strcmp(token, ">")==0 ) 
      {
        if (strcmp(token, ">")==0) write2file = true;
        commands[indexL++][indexP]=NULL;
        indexP=0;
      }
      else 
      {
        commands[indexL][indexP++] = token;
      }
      token = strtok(NULL, space);
    }

    commands[indexL][indexP] = NULL;
    if(commands[indexL][indexP-1][0]=='|' or commands[indexL][indexP-1][0] == '!') 
    {
        if(commands[indexL][indexP-1][0]=='|') execType = 4;
        else execType = 5;
        commands[indexL+1][0] = commands[indexL][indexP-1];
        commands[indexL++][indexP-1] = NULL;
    }

    /* execute commands */
    //built-in commands
    for(int i=0; i<3; i++) {
      if (strcmp(commands[0][0],builtIn_command[i])==0) 
      {
        execType = -1;
        if(i == 0) setenv(commands[0][1], commands[0][2], 1);
        else if(i == 1) {
          if(getenv(commands[0][1]) != NULL) 
          {
            strcpy(buf, getenv(commands[0][1]));
            write(client_fd, buf, strlen(buf));
            write(client_fd, "\n", 1);
          }
        }
        else if(i == 2) 
        {
          waitpid(-1, &wstatus, WNOHANG);
          return 0;  // 讓a=0,停止shell
        }
        break;
      }
    }
    for (int i = tail; i < head; i++) 
    {
      if (NumPipeQueue[i][0] >= 0) NumPipeQueue[i][0]--;  // count--
    }
    if (NumPipeQueue[tail][0] == 0 && tail != head) is_jump = true;
    int IN = 0;
    if (is_jump)  
    {
        IN = NumPipeQueue[tail][1];
        close(NumPipeQueue[tail++][2]);
    }
    
    if (execType == 1) 
    { /****************** normal pipe ********************/
      int pipefd[2];
      pid_t child_pid;
      for (int i = 0; i <= indexL; i++) 
      {
        if(pipe(pipefd) < 0) 
        {
            cerr << "Could not create pipe\n";
            break;
        }
        while ((child_pid = fork()) < 0) 
        {
            waitpid(-1, &wstatus, 0);
        }
        if (child_pid == 0) 
        {
          //child
          if (write2file && i == indexL) 
          {
            int filefd = open(commands[i][0], FLAGS, mode);
            
            while ( read_len = read(IN, buf, 1024)) 
            {
              write(filefd, buf, read_len);
              memset(buf, 0, 1024);
            }
            close(pipefd[0]);
            close(pipefd[1]);
            close(IN);
            close(filefd);
            exit(EXIT_SUCCESS);
          }
          dup2(IN, STDIN_FILENO);
          if (i != indexL) {
            dup2(client_fd,STDERR_FILENO);
            dup2(pipefd[1],STDOUT_FILENO);
          }
          else 
          {
            dup2(client_fd, STDERR_FILENO);
            dup2(client_fd, STDOUT_FILENO);
          }
          close(pipefd[0]);
          close(pipefd[1]);
          execvp(commands[i][0], commands[i]);
          dup2(client_fd, STDERR_FILENO);
          cerr << "Unknown command: [" << commands[i][0] << "].\n";
          exit(EXIT_FAILURE);
        }
        //parent
        if (i != 0 && i % PIPEFDN == 0) 
        {
            waitpid(child_pid, &wstatus, WNOHANG);
            for (int j = 1; j < PIPEFDN; j++) close(NoUseNumPipe[j]);
        }
        if (i > 0) NoUseNumPipe[i % PIPEFDN] = IN;
        close(pipefd[1]);
        IN = pipefd[0];
      }
      close(IN);
      waitpid(child_pid, &wstatus, 0);
    }
    else if (execType == 4 || execType == 5) 
    { /****************** number pipe ********************/
      if (execType == 4) token = strtok(commands[indexL][0], "|");
      else token = strtok(commands[indexL][0], "!");
      int jump = atoi(token), pipefd[2], OUT = 0;
      pid_t child_pid;
      for (int i = 0; i < indexL; i++) 
      {
        if (pipe(pipefd) < 0) 
        {
            cerr << "could not create pipe!\n";
            break;
        }

        while((child_pid = fork()) < 0) wait(&wstatus);
        
        if (child_pid == 0) 
        {
          dup2(IN, STDIN_FILENO);
          if (execType == 5) 
          {
            dup2(pipefd[1], STDERR_FILENO);
          }
          else 
          {
            dup2(client_fd, STDERR_FILENO);
          }
          dup2(pipefd[1], STDOUT_FILENO);
          close(pipefd[0]);
          close(pipefd[1]);
          execvp(commands[i][0], commands[i]);
          dup2(client_fd, STDERR_FILENO);
          cerr << "Unknown command: [" << commands[i][0] << "].\n";
          exit(EXIT_FAILURE);
        }
        if (i != 0 && i % PIPEFDN == 0) 
        {
          waitpid(child_pid, &wstatus, WNOHANG);
          for (int j = 1; j < PIPEFDN; j++) close(NoUseNumPipe[j]);
        }
        if (i > 0) NoUseNumPipe[i % PIPEFDN] = IN;
        if (i != indexL-1) close(pipefd[1]);
        OUT = pipefd[1];
        IN  = pipefd[0];
      }
      bool is_insert = false;  // number pipe 大小排序
      if (head >= MAXNUMBERPIPE ) {
        cerr << "numberPipeQueue overflow\n";
        exit(EXIT_SUCCESS);
      }
      else 
      { // NumPipeQueue 排序
        for (int i = tail; i < head; i++) 
        {
          if (jump > NumPipeQueue[i][0]) continue;
          else if (jump < NumPipeQueue[i][0])
          {
            for (int j = head; j > i; j--) 
            {
              NumPipeQueue[j][0] = NumPipeQueue[j-1][0];
              NumPipeQueue[j][1] = NumPipeQueue[j-1][1];
              NumPipeQueue[j][2] = NumPipeQueue[j-1][2];
            }
            is_insert = true;
            NumPipeQueue[i][0] = jump;
            NumPipeQueue[i][1] = IN;
            NumPipeQueue[i][2] = OUT;
            head++;
            break;
          }
          else // 如果jump剛好==numberPipeQueue[i]，那就直接把結果寫入該numberPipeQueue[i]的FD[2]
          {
            is_insert = true;
            while((child_pid = fork()) < 0) wait(&wstatus);
            
            if (child_pid == 0) 
            {
              int read_len = 0;
              char buf[1024] = {0};
              close(OUT);
              while ( read_len = read(IN, buf, 1024)) 
              {
                  write(NumPipeQueue[i][2], buf, read_len);
                  memset(buf, 0, 1024);
              }
              close(IN);
              exit(EXIT_SUCCESS);
            }
            close(IN);
            close(OUT);
            break;
          }
        }
        if(!is_insert) 
        {  // 要插入的jump最大，直接插在最後(head)
          NumPipeQueue[head][0]   = jump;
          NumPipeQueue[head][1]   = IN;
          NumPipeQueue[head++][2] = OUT;
        }
      }
    }
    while(waitpid(-1, NULL, WNOHANG)>0){};
  }
  return 1;
}

void reaper(int sig) 
{
  int status;
  while(wait3(&status, WNOHANG, (struct rusage *)0) >= 0 ) {} // wait3() 等待所有進程
}

int TCPechod(int fd) 
{
  //shell init
  head = 0;
  tail = 0;
  setenv("PATH", "bin:.", 1);
  int a = 1;
  while (a) {
    if (write(fd, "% ", 2) < 0) {
      cerr << "ecoh write: " << strerror(errno) << endl;
      exit(EXIT_FAILURE);
    }
    a = shell(fd);
  }
  return 0;
}

int passiveTCP(const char *service, int qlen) 
{
  string str_temp = "tcp";
  char protocol[str_temp.length() + 1];
  strcpy(protocol, str_temp.c_str());
  return passivesock(service, protocol, qlen);
}

int passivesock(const char *service, char *protocol, int qlen) 
{
  struct servent *pse; //pointer to service info entry
  struct protoent *ppe; // pointer to protocol info entry
  struct sockaddr_in sin; // an Internet endpoint address
  int s, type; // socket descriptor, socket type

  bzero((char *)&sin, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = INADDR_ANY;

  // Map service name to port number
  if(pse = getservbyname(service, protocol))
    sin.sin_port = htons(ntohs((u_short)pse->s_port) + portbase);
  else if ((sin.sin_port = htons((u_short)atoi(service))) ==0) {
    cerr << "can't get " << protocol << "protocol entry\n";
    exit(EXIT_FAILURE);
  }
  //Map protocol name to protocol number
  if ((ppe = getprotobyname(protocol)) ==0) {
    cerr << "can't get " << protocol << "protocol entry\n";
    exit(EXIT_FAILURE);
  }
  // Use protocol to choose a socket type
  if (strcmp(protocol, "udp")==0)
    type = SOCK_DGRAM;
  else
    type = SOCK_STREAM;

  // Allocate a socket
  s = socket(PF_INET, type, ppe->p_proto);
  if(s < 0) {
    cerr << "can't not create socket: " << strerror(errno) << endl;
    exit(EXIT_FAILURE);
  }
  //Setsockopt to allow the port to be reused.
  int enable = 1;
  if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
    cerr <<"setsockopt(SO_REUSEADDR) failed.\n";
    exit(EXIT_FAILURE);
  }
  // Bind the socket
  if (bind(s, (struct sockaddr *)&sin, sizeof(sin))<0) {
    cerr << "can't bind to " << service << " port: " << strerror(errno) << endl;
    exit(EXIT_FAILURE);
  }
  // Listen
  if (type == SOCK_STREAM) {
    if (listen(s, qlen) < 0) {
      cerr << "can't listen on " << service << " port: " << strerror(errno) << endl;
      exit(EXIT_FAILURE);
    }
  }
  return s;
}

int readline(int fd, char *ptr, int maxlen) 
{
  int n, rc;
  char c;
  for (n = 1;n < maxlen;) {
    if ((rc=read(fd, &c, 1)) == 1) {
      if (c != '\r' && c != '\n') {
        *ptr++ = c;
        n++;
      }
      if(c == '\n') break;
    }
    else if (rc == 0) {
      if (n == 1) return 0;
      else break;
    }
    else return -1;
  }
  
  *ptr = 0;
  return n;
}

vector<string> split_cmdline_by_numpipe(string cmdline)
{
    bool isnumpipe=false;
    vector<string> tempcommands;
    int p=0,q=0;
    for(int i=0;i<cmdline.length();i++){
        if(cmdline[i]=='|' || cmdline[i]=='!'){
            isnumpipe=true;
        }
        if(cmdline[i]=='>'){
            isnumpipe=false;
        }
        if(isnumpipe & isdigit(cmdline[i])==0 & isdigit(cmdline[i-1])==1){
            q=i;
            tempcommands.push_back(cmdline.substr(p,q-p));
            p=q+1;
            isnumpipe=false;
        }
    }
    if(p != cmdline.length()){ //p沒到底
        tempcommands.push_back(cmdline.substr(p,cmdline.length()));
    }
    //for(int i=0;i<tempcommands.size();i++){cout<<tempcommands[i]<<" @@@ "<<endl;}

    return tempcommands;
}

bool replace(string& str, const string& from, const string& to) 
{
    size_t start_pos = str.find(from);
    if(start_pos == string::npos)
        return false;
    str.replace(start_pos, from.length(), to);
    return true;
}