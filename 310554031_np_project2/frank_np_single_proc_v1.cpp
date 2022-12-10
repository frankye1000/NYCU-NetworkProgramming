#include<iostream>
#include<fstream>
#include<string>
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

const char *builtIn_command[7] = { "setenv", "printenv", "exit", "who", "tell", "yell", "name" };
extern int errno;
u_short portbase = 0;

struct numberPipe {
  int q[MAXNUMBERPIPE][3] = {};
  int h = 0;
  int t = 0;
};

struct userInfo {
  int id;
  int fd;
  char name[20] = "(no name)";
  char ip[INET_ADDRSTRLEN];
  unsigned int port;
  struct numberPipe np;
  char envPath[10] = "PATH";
  char env[100] = "bin:.";
  int send_up[30];
  int get_from[30];
};
 
struct userInfo userInfoArray[QLEN];
bool idSet[QLEN] = {false};  

int shell(int client_fd);
void reaper(int);
int TCPechod(int fd);
int passiveTCP(const char *service, int qlen);
int passivesock(const char *service, char *protocol, int qlen);
int readline(int fd, char *ptr, int maxlen);
void showInitMSG(int fd);
void showLoginMSG(int id);
void who(int id);
void tell(int client_id, char* id, char* msg);
void yell(int id, char* msg);
void name(int id, char* newName);
void broadcast(char *msg);

int main(int argc, char *argv[]) 
{
    char *service = argv[1]; /* service name or port number */
    struct sockaddr_in fsin; /* client address */
    bool is_getId = false;
    socklen_t AddrLen; // length of client addr 
    fd_set rfds; //read fd set
    fd_set afds; //active fd set
    int fd, nfds, msock;
    
    setenv("PATH", "bin:.", 1);
    for(int i=0; i<QLEN; i++) idSet[i] = false;
    
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
    
    msock = passiveTCP(service, QLEN);
    nfds  = FD_SETSIZE;   // 1024
    FD_ZERO(&afds);       // 清空afds
    FD_SET(msock, &afds); // afds增加master socket ???? 為什麼需要將master fd 加入afds???
    //signal(SIGCHLD, reaper);
    while(1) 
    {
      memcpy(&rfds, &afds, sizeof(rfds));  // 將afds複製進rfds
      // nfds:需要監視的最大的文件描述符值+1, &rfds:需要檢測的可讀文件描述符的集合, struct timeval結構用於描述一段時間長度，如果在這個時間內，需要監視的描述符沒有事件發生則函數返回，返回值為0。
      if(select(nfds, &rfds, (fd_set*)0, (fd_set*)0, (struct timeval*)0)<0) {   // 監聽rfds裡面是否有可讀條件待處裡
        cerr << "select: " << strerror(errno) <<endl;
        exit(EXIT_FAILURE);
      }
      
      if(FD_ISSET(msock, &rfds)) {    // master sock需要處理
        int ssock;                    // client fd
        is_getId = false;
        AddrLen  = sizeof(fsin);
        ssock    = accept(msock, (struct sockaddr *)&fsin, &AddrLen);   // wait slaver connection....
        if(ssock<0) {
          cerr << "accept: " << strerror(errno) << endl;
          exit(EXIT_FAILURE);
        }
        puts("Connection accepted");
        //bzero(&fsin, sizeof(fsin));   // 清空 client address 
        //getpeername(ssock, (struct sockaddr *)&fsin, &AddrLen); 

        for(int i=0; i<QLEN; i++) 
        {   // initial set user info 
          if(!idSet[i]) 
          {             // idSet用來判斷是否已有init user資訊
            idSet[i] = true;
            is_getId = true;
            userInfoArray[i].id = i+1;
            userInfoArray[i].fd = ssock;
            strcpy(userInfoArray[i].name, "(no name)");
            strcpy(userInfoArray[i].ip, inet_ntoa(fsin.sin_addr));
            userInfoArray[i].port = ntohs(fsin.sin_port);
            userInfoArray[i].np.q[0][0] = 0;  // set number pipe
            userInfoArray[i].np.h = 0;
            userInfoArray[i].np.t = 0;
            strcpy(userInfoArray[i].env, "bin:.");
            strcpy(userInfoArray[i].envPath, "PATH");
            for(int j=0; j<QLEN; j++) {
              userInfoArray[i].send_up[j] = -1;
              userInfoArray[i].get_from[j]  = -1;
            }
            break;
          }
        }
        if(!is_getId) {  // 超過30個client
          close(ssock);
          continue;
        }
        showInitMSG(ssock);
        showLoginMSG(ssock);
        if (write(ssock, "% ", 2) < 0) 
        {
          cerr << "ecoh write: " << strerror(errno) << endl;
          exit(EXIT_FAILURE);
        }
        FD_SET(ssock, &afds);  // 增加slaver fd
      }
      // 其他client fd需要處理
      for(fd=0; fd<nfds; ++fd) 
      {
        if(fd != msock && FD_ISSET(fd, &rfds)) 
        {
          if(TCPechod(fd)==0) 
          {    // clientfd去做事情
            close(fd);             // clientfd關掉
            FD_CLR(fd, &afds);     // 把clientfd從afds刪除
            string exitmsg;
            for(int i=0; i<QLEN; i++) 
            {
              if(userInfoArray[i].fd == fd) 
              {
                idSet[i] = false;
                // clean user pipe  
                for(int j=0; j<QLEN; j++) 
                {           
                  if(userInfoArray[i].send_up[j] != -1) 
                  {  
                    close(userInfoArray[i].send_up[j]);     // close i>j
                    close(userInfoArray[j].get_from[i]);    // close j>i
                    userInfoArray[i].send_up[j] = -1;
                    userInfoArray[j].get_from[i]  = -1;
                  }
                  if(userInfoArray[i].get_from[j] != -1) 
                  {
                    close(userInfoArray[i].get_from[j]);    // close j>i
                    close(userInfoArray[j].send_up[i]);     // close i>j
                    userInfoArray[i].get_from[j]  = -1;
                    userInfoArray[j].send_up[i] = -1;
                  }
                }
                //broadcast exit msg
                exitmsg = "*** User '" + string(userInfoArray[i].name) + "' left. ***\n";
                char c[strlen(exitmsg.c_str())];
                strcpy(c, exitmsg.c_str());
                broadcast(c);
                break;
              }
            }
          }
          else {
            if (write(fd, "% ", 2) < 0) 
            {
              cerr << "ecoh write: " << strerror(errno) << endl;
              exit(EXIT_FAILURE);
            }
          }
        }
      } //for(fd)
    } //while(1)
    return 0;
} //main

int shell(int client_fd) 
{
  string str_temp = "/dev/null";
  char DevNull[str_temp.length() + 1];
  strcpy(DevNull, str_temp.c_str());

  int cmdlineLength;
  char *commands[2000][10], input[15001], *token, space[2]=" ";
  int execFlag = 1, indexL = 0, indexP = 0, wstatus;
  bool is_jump = false, write2file = false;
  int NoUseNumPipe[PIPEFDN];
  int read_len = 0;
  char buf[1024] = {0};
  int client_id;
  int get_from_id = -1, send_up_id = -1;
  bool is_send_err = false, is_get_err = false;
  
  // set user env and number pipe
  for(int i=0; i<QLEN; i++) {
    if(userInfoArray[i].fd==client_fd) {
      setenv(userInfoArray[i].envPath, userInfoArray[i].env, 1);
      client_id = userInfoArray[i].id;
      break;
    }
  }
  
  //read command
  cmdlineLength = readline(client_fd, input, sizeof input);
  if(cmdlineLength < 0){
    cerr << "echo read: " << strerror(errno) << endl;
    exit(EXIT_FAILURE);
  }
  else if (cmdlineLength == 1) return 1;
  
  /* parse the commands */
  string s = string(input);
  int len = 0;
  token = strtok(input, space);
  if(strcmp(token, "tell") == 0) {
    commands[0][0] = token;
    len += strlen(token);
    while(s[len] == ' ') len++;
    token = strtok(NULL, space);
    commands[0][1] = token;           // user id 
    // 這邊要判斷名稱 //
	  // puts(token);
    // for (int i = 0; i < 30; i++){
  	// 	puts(userInfoArray[i].name);
    //   if (strcmp(userInfoArray[i].name, token)==0){
    //     puts("YESSS");
    //     char *temp=NULL;
    //     strcpy(temp,to_string(userInfoArray[i].id).c_str());
    //     puts(temp);
  	// 		commands[0][1] = temp;
			  
    //     break;
  	// 	}
  	// }
	
	
	
	
	
	
	///////////////////
	  len += strlen(token);
    while(s[len] == ' ') len++;
    commands[0][2] = strdup(s.substr(len,s.length()-len).c_str());
  }
  else if(strcmp(token, "yell") == 0) {
    commands[0][0] = token;
    len += strlen(token);
    while(s[len] == ' ') len++;
    commands[0][1] = strdup(s.substr(len,s.length()-len).c_str());
  }
  else {
    while(token != NULL) {
      if(strcmp(token,"|")==0 || (strcmp(token, ">")==0 && strlen(token)== 1 ) ) { 
  		  if (strcmp(token, ">")==0) write2file = true;
  		  commands[indexL++][indexP] = NULL;
  		  indexP=0;
      }
      //send user pipe
      else if (token[0] == '>') {
        string a = string(token);
        a = a.substr(1, a.length()-1);    // user id
        send_up_id = stoi(a);             // send >user id
        commands[indexL][indexP] = NULL;
        execFlag = 6;
        //the user id does not exist
        if (send_up_id > QLEN || send_up_id <= 0 || !idSet[send_up_id-1]) 
        {
          string errmsg = "*** Error: user #"+to_string(send_up_id)+" does not exist yet. ***\n";
          write(client_fd, errmsg.c_str(), strlen(errmsg.c_str()));
          if(!is_send_err && !is_get_err)  // 
            commands[indexL][indexP++] = DevNull;
          is_send_err = true;
        }
        //the user pipe already exist  要等對方收完，才能再傳
        if(userInfoArray[client_id-1].send_up[send_up_id-1] != -1) 
        {
          string errmsg = "*** Error: the pipe #"+to_string(client_id)+"->#"+to_string(send_up_id)+" already exists. ***\n";
          write(client_fd, errmsg.c_str(), strlen(errmsg.c_str()));
          if(!is_send_err && !is_get_err)  // 
            commands[indexL][indexP++] = DevNull;
          is_send_err = true;
        }
      }
      //get user pipe
      else if(token[0] == '<') 
      {
        string a = string(token);
        a = a.substr(1, a.length()-1);       // user id
        get_from_id = stoi(a);               // get <user id
        commands[indexL][indexP] = NULL;
        //the user id does not exist
        if (get_from_id > QLEN || get_from_id <= 0 || !idSet[get_from_id-1]) 
        {
          string errmsg = "*** Error: user #"+to_string(get_from_id)+" does not exist yet. ***\n";
          write(client_fd, errmsg.c_str(), strlen(errmsg.c_str()));
          if(!is_send_err && !is_get_err)
            commands[indexL][indexP++] = DevNull;
          is_get_err = true;
        }
        //the user pipe does not exist
        else if(userInfoArray[client_id-1].get_from[get_from_id-1] == -1) 
        {
          string errmsg = "*** Error: the pipe #"+to_string(get_from_id)+"->#"+to_string(client_id)+" does not exist yet. ***\n";
          write(client_fd, errmsg.c_str(), strlen(errmsg.c_str()));
          if(!is_send_err && !is_get_err)
            commands[indexL][indexP++] = DevNull;
          is_get_err = true;
        }
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
  	    if(commands[indexL][indexP-1][0]=='|') execFlag = 4;
  	    else execFlag = 5;
  	    commands[indexL+1][0] = commands[indexL][indexP-1];
  	    commands[indexL++][indexP-1] = NULL;
  	}
  }
	/* execute commands */
	//built-in commands
  for(int i=0; i<7; i++) 
  {
    if (strcmp(commands[0][0],builtIn_command[i])==0) 
    {
      execFlag = -1;
      if(i == 0) 
      {  //setenv
        strcpy(userInfoArray[client_id-1].envPath, commands[0][1]);
        strcpy(userInfoArray[client_id-1].env, commands[0][2]);
      }
      else if(i == 1) 
      { // printenv
        if(getenv(userInfoArray[client_id-1].envPath) != NULL) 
        {
          strcpy(buf, getenv(commands[0][1]));
          write(client_fd, buf, strlen(buf));
          write(client_fd, "\n", 1);
        }
      }
		  else if(i == 2) 
      { //exit
		    waitpid(-1, &wstatus, WNOHANG);
		    return 0;
	    }
      else if(i == 3) 
      { //who
        who(client_id);
      }
      else if(i == 4) 
      { //tell

        tell(client_id, commands[0][1], commands[0][2]);
      }
      else if(i == 5) 
      { //yell
        yell(client_id, commands[0][1]);
      }
      else if(i == 6) 
      { //name
        name(client_id, commands[0][1]);
      }
		  break;
    }
	}
	for (int i = userInfoArray[client_id-1].np.t; i < userInfoArray[client_id-1].np.h; i++) 
  {
    if (userInfoArray[client_id-1].np.q[i][0] >= 0) userInfoArray[client_id-1].np.q[i][0]--;    // count--
	}
	if (userInfoArray[client_id-1].np.q[userInfoArray[client_id-1].np.t][0] == 0 && userInfoArray[client_id-1].np.t != userInfoArray[client_id-1].np.h) is_jump = true;  // 要跳了!!
	
  int IN = 0;
  // number pipe need to output
	if (is_jump)   
  {
    IN = userInfoArray[client_id-1].np.q[userInfoArray[client_id-1].np.t][1];
    close(userInfoArray[client_id-1].np.q[userInfoArray[client_id-1].np.t++][2]);
	}

  // get user pipe
  if(get_from_id != -1 && !is_get_err) 
  { 
    IN = userInfoArray[client_id-1].get_from[get_from_id-1];
    close(userInfoArray[get_from_id-1].send_up[client_id-1]);  // 要記得關掉
    userInfoArray[client_id-1].get_from[get_from_id-1]  = -1;  // 如果讀取完就清掉get_from
    userInfoArray[get_from_id-1].send_up[client_id-1] = -1;    // 如果讀取完就清掉send_up 
    // broadcast msg
    string ss = "*** " + string(userInfoArray[client_id-1].name) + " (#"+to_string(client_id)+") just received from " + string(userInfoArray[get_from_id-1].name) + " (#"+to_string(get_from_id)+") by '" + s + "' ***\n";
    const char* coc = ss.c_str();
    char c[strlen(coc)];
    strcpy(c, coc);
    broadcast(c); 
  }
	if (execFlag == 1) 
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
  		
      while ((child_pid = fork()) < 0) waitpid(-1, &wstatus, 0);
  		
      if (child_pid == 0) 
      { //child
  	    if (write2file && i == indexL) {
    			int filefd = open(commands[i][0], FLAGS, mode);
    			
    			while ( read_len = read(IN, buf, 1024)) {
    		    write(filefd, buf, read_len);
    		    memset(buf, 0, 1024);
    			}
    			close(pipefd[0]);
    			close(pipefd[1]);
    			close(IN);
    			close(filefd);
    			exit(EXIT_SUCCESS);
        }
        dup2(IN,STDIN_FILENO);
        if (i != indexL) {
          dup2(client_fd,STDERR_FILENO);
    			dup2(pipefd[1],STDOUT_FILENO);
        }
        else {
          dup2(client_fd,STDERR_FILENO);
          dup2(client_fd,STDOUT_FILENO);
        }
  	    close(pipefd[0]);
  	    close(pipefd[1]);
  	    execvp(commands[i][0], commands[i]);
        dup2(client_fd,STDERR_FILENO);
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
	else if (execFlag == 4 || execFlag == 5) 
  { /****************** number pipe ********************/
    if (execFlag == 4) token = strtok(commands[indexL][0], "|");
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
		    if (execFlag == 5) 
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
  		if (i != 0 && i % PIPEFDN == 0) // 每500個清理掉number pipe
      {
		    waitpid(child_pid, &wstatus, WNOHANG);
		    for (int j = 1; j < PIPEFDN; j++) close(NoUseNumPipe[j]);
  		}
  		if (i > 0) NoUseNumPipe[i % PIPEFDN] = IN;
  		if (i != indexL-1) close(pipefd[1]);
  		OUT = pipefd[1];
  		IN  = pipefd[0];
    }
    bool is_insert = false;
    if (userInfoArray[client_id-1].np.h >= MAXNUMBERPIPE ) 
    {
  		cerr << "numberpipe overflow\n";
  		exit(EXIT_SUCCESS);
    }
    else 
    { // NumPipeQueue 排序
  		for (int i = userInfoArray[client_id-1].np.t; i < userInfoArray[client_id-1].np.h; i++) 
      {
		    if (jump > userInfoArray[client_id-1].np.q[i][0]) continue;
		    else if (jump < userInfoArray[client_id-1].np.q[i][0])
        {
    			for (int j = userInfoArray[client_id-1].np.h; j > i; j--) 
          {
    		    userInfoArray[client_id-1].np.q[j][0] = userInfoArray[client_id-1].np.q[j-1][0];
    		    userInfoArray[client_id-1].np.q[j][1] = userInfoArray[client_id-1].np.q[j-1][1];
    		    userInfoArray[client_id-1].np.q[j][2] = userInfoArray[client_id-1].np.q[j-1][2];
    			}
    			is_insert = true;
    			userInfoArray[client_id-1].np.q[i][0] = jump;
    			userInfoArray[client_id-1].np.q[i][1] = IN;
    			userInfoArray[client_id-1].np.q[i][2] = OUT;
    			userInfoArray[client_id-1].np.h++;
    			break;
		    }
		    else  // 如果jump剛好==numberPipeQueue[i]，那就直接把結果寫入該numberPipeQueue[i]的FD[2]
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
  			        write(userInfoArray[client_id-1].np.q[i][2], buf, read_len);
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
      { // 要插入的jump最大，直接插在最後(head)
  	    userInfoArray[client_id-1].np.q[userInfoArray[client_id-1].np.h][0]   = jump;
  	    userInfoArray[client_id-1].np.q[userInfoArray[client_id-1].np.h][1]   = IN;
  	    userInfoArray[client_id-1].np.q[userInfoArray[client_id-1].np.h++][2] = OUT;
  		}
    }
  }
  else if (execFlag == 6) 
  { /****************** send user pipe ********************/
    int pipefd[2], OUT = 0;
    pid_t child_pid;
    for (int i = 0; i <= indexL; i++) 
    {
  		if(pipe(pipefd) < 0) 
      {
          cerr << "Could not create pipe\n";
  		    break;
  		}
  		while ((child_pid = fork()) < 0) waitpid(-1, &wstatus, 0);
  		    
  		if (child_pid == 0) 
      {
  	    //child
        dup2(IN,STDIN_FILENO);
        dup2(client_fd,STDERR_FILENO);
  			dup2(pipefd[1],STDOUT_FILENO);
  	    close(pipefd[0]);
  	    close(pipefd[1]);
  	    execvp(commands[i][0], commands[i]);
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
  		if (i != indexL) close(pipefd[1]);
  		OUT = pipefd[1];
  		IN  = pipefd[0];
    }

    if(!is_send_err) // send to user exist
    {
      userInfoArray[client_id-1].send_up[send_up_id-1] = OUT;
      userInfoArray[send_up_id-1].get_from[client_id-1]= IN;
    }
    else 
    {
      close(OUT);
      close(IN);
    }
	}
  // broadcast send msg
  if(send_up_id != -1 && !is_send_err) 
  {
    string ss = "*** "+string(userInfoArray[client_id-1].name)+" (#"+to_string(client_id)+") just piped '"+s+"' to "+string(userInfoArray[send_up_id-1].name)+" (#"+to_string(send_up_id)+") ***\n";
    const char* coc = ss.c_str();
    char c[strlen(coc)];
    strcpy(c, coc);
    broadcast(c);
  }
  while(waitpid(-1, NULL, WNOHANG)>0){};
  return cmdlineLength;
}

void reaper(int sig) 
{
  int status;
  while(wait3(&status, WNOHANG, (struct rusage *)0) >= 0 ) {}
}

int TCPechod(int fd) 
{
  return shell(fd);
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
  sin.sin_family      = AF_INET;
  sin.sin_addr.s_addr = INADDR_ANY;
  
  // Map service name to port number
  if(pse = getservbyname(service, protocol))
    sin.sin_port = htons(ntohs((u_short)pse->s_port) + portbase);
  else if ((sin.sin_port = htons((u_short)atoi(service))) ==0) 
  {
    cerr << "can't get " << protocol << "protocol entry\n";
    exit(EXIT_FAILURE);
  }
  //Map protocol name to protocol number
  if ((ppe = getprotobyname(protocol)) ==0) 
  {
    cerr << "can't get " << protocol << "protocol entry\n";
    exit(EXIT_FAILURE);
  }
  // Use protocol to choose a socket type
  if (strcmp(protocol, "udp")==0) type = SOCK_DGRAM;
  else type = SOCK_STREAM;
  
  // Allocate a socket
  s = socket(PF_INET, type, ppe->p_proto);
  if(s < 0) 
  {
    cerr << "can't not create socket: " << strerror(errno) << endl;
    exit(EXIT_FAILURE);
  }
  //Setsockopt to allow the port to be reused.
  int enable = 1;
  if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) 
  {
    cerr <<"setsockopt(SO_REUSEADDR) failed.\n";
    exit(EXIT_FAILURE);
  }
  // Bind the socket
  if (bind(s, (struct sockaddr *)&sin, sizeof(sin))<0) 
  {
    cerr << "can't bind to " << service << " port: " << strerror(errno) << endl;
    exit(EXIT_FAILURE);
  }
  if (type == SOCK_STREAM) 
  {
    if (listen(s, qlen) < 0) 
    {
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
  for (n = 1;n < maxlen;) 
  {
    if ((rc=read(fd, &c, 1)) == 1) 
    {
      if (c != '\r' && c != '\n')
      {
        *ptr++ = c;
        n++;
      }
      if(c == '\n') break;
    }
    else if (rc == 0) 
    {
      if (n == 1) return 0;
      else break;
    }
    else return -1;
  }
  *ptr = 0;
  return n;
}

void showInitMSG(int fd) 
{
  const char *welcomeMSG = "****************************************\n** Welcome to the information server. **\n****************************************\n";
  write(fd, welcomeMSG, strlen(welcomeMSG));
}

void showLoginMSG(int fd) 
{
  for(int i=0; i<QLEN; i++) 
  {
    if(idSet[i] && userInfoArray[i].fd == fd) 
    {
      string s = "*** User '" + string(userInfoArray[i].name) + "' entered from " + string(userInfoArray[i].ip) + ":" + to_string(userInfoArray[i].port)+". ***\n";
      char c[strlen(s.c_str())];
      strcpy(c, s.c_str());
      broadcast(c);
      break;
    }
  }
}

void broadcast(char *msg) 
{
  for (int i=0; i<QLEN; i++) 
  {
    if(idSet[i]) write(userInfoArray[i].fd, msg, strlen(msg));
  }
}

void who(int id) 
{
  string str = "<ID>\t<nickname>\t<IP:port>\t<indicate me>\n";
  for(int i=0; i<QLEN; i++) 
  {
    if(idSet[i]) 
    {
      str += to_string(userInfoArray[i].id)+"\t" + string(userInfoArray[i].name)+"\t" + string(userInfoArray[i].ip) + ":" + to_string(userInfoArray[i].port);
      if(id == i+1) str += "\t<-me";   // 如果剛好是我，就要在字串後面<-me
      str += "\n";
    }
  }
  write(userInfoArray[id-1].fd, str.c_str(), str.length());
}

void tell(int client_id, char* uiods, char* msg) 
{
  string s;
  const char* c;
  
  if(strlen(uiods)==1){
    int id = atoi(uiods);
    if(!idSet[id-1]) 
    {
      s = "*** Error: user #" + to_string(id) + " does not exist yet. ***\n";
      c = s.c_str();
      char errmsg[strlen(c)];
      strcpy(errmsg, c);
      write(userInfoArray[client_id-1].fd, errmsg, strlen(errmsg));
      return;
    }
    
    s = "*** " + string(userInfoArray[client_id-1].name) + " told you ***: " + string(msg) + "\n";
    c = s.c_str();
    char personMSG[strlen(c)];
    strcpy(personMSG, c);
    write(userInfoArray[id-1].fd, personMSG, strlen(personMSG));
  }else{
    char *temp=NULL;
    int uuuu;
    for (int i = 0; i < 30; i++){
  		puts(userInfoArray[i].name);
      if (strcmp(userInfoArray[i].name, uiods)==0){
        puts("YESSS");
        
        uuuu = userInfoArray[i].id;
        
  			
			  puts("YES123456");
        break; 
  		}
  	}

    
    if(!idSet[uuuu-1]) 
    {
      s = "*** Error: user #" + to_string(uuuu) + " does not exist yet. ***\n";
      c = s.c_str();
      char errmsg[strlen(c)];
      strcpy(errmsg, c);
      write(userInfoArray[client_id-1].fd, errmsg, strlen(errmsg));
      return;
    }
    
    s = "*** " + string(userInfoArray[client_id-1].name) + " told you ***: " + string(msg) + "\n";
    c = s.c_str();
    char personMSG[strlen(c)];
    strcpy(personMSG, c);
    write(userInfoArray[uuuu-1].fd, personMSG, strlen(personMSG));



    }


  
}

void yell(int id, char* msg) 
{
  string s;
  const char* c;
  s = "*** " + string(userInfoArray[id-1].name) + " yelled ***: " + string(msg)+"\n";
  c = s.c_str();
  char yellMSG[strlen(c)];
  strcpy(yellMSG, c);
  broadcast(yellMSG);
}

void name(int id, char* newName) 
{
  for (int i=0; i<QLEN;i++) 
  {
    if(idSet[i] && userInfoArray[i].id != id) 
    {
      if(strcmp(newName, userInfoArray[i].name) == 0) 
      {
        string errstr = "*** User '" + string(newName) + "' already exists. ***\n";
        const char* c = errstr.c_str();
        char errmsg[strlen(c)];
        strcpy(errmsg, c);
        write(userInfoArray[id-1].fd, errmsg, strlen(errmsg));
        return;
      }
    }
  }
  strcpy(userInfoArray[id-1].name, newName);
  string s = "*** User from " + string(userInfoArray[id-1].ip) + ":" + to_string(userInfoArray[id-1].port) + " is named '" + string(userInfoArray[id-1].name)+"'. ***\n";
  char c[strlen(s.c_str())];
  strcpy(c, s.c_str());
  broadcast(c);
}