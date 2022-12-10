/* version 1, 可過三個測資, 但無法重複連線測試 */
#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
using namespace std;

#include <stdio.h>
#include <stdlib.h>
#include <string.h>    
#include <unistd.h>    // pipe、dup2、read、write、open
#include <sys/wait.h>  // waitpid
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h> //inet_addr


#define PIPEFDN 500
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
// open file
#define FLAGS O_RDWR|O_CREAT|O_TRUNC   
// O_APPEND: The file is opened in append mode. Before each write(2), the file offset is positioned at the end of the file, as if with lseek(2).
// O_RDWR: 以讀寫模式開啟,
// O_CREAT: 如果指定的檔案不存在，就建立一個,
// O_TRUNC: 若開啟的檔案存在且是一般檔案，開啟後就將其截短成長度為 0

#define mode 0666     // mode: Unix 權限位元設定

const vector<string> split(const string &str, const char &delimiter);
vector<string> split_cmdline_by_numpipe(string cmdline);
bool replace(string& str, const string& from, const string& to);
void InitEnv();
int readline(int fd, char *ptr, int maxlen);


struct Num_pipe{
    int count = -1;
    int fd[2];
};


int main(int argc , char *argv[]){
    int port = atoi(argv[1]);
    int socketfd,client_sock,c,read_size;
    struct sockaddr_in server , client;
    char bufff[20000]={0};

    //Create socket
    socketfd = socket(AF_INET , SOCK_STREAM , 0);
    if (socketfd == -1){
        cout<<"Could not create socket"<<endl;
    }
    puts("Socket created");
    
    //Prepare the sockaddr_in structure
    server.sin_family      = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port        = htons(port);

    int option = 1;
    if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(int)) < 0)
        cerr<<"setsockopt(SO_REUSEADDR) failed"<<endl;

    //Bind
    if( bind(socketfd,(struct sockaddr *)&server , sizeof(server)) < 0){
        //print the error message
        cerr<<"bind failed. Error"<<endl;
        return 1;
    }
    puts("bind done");

    //Listen
    listen(socketfd , 3);

    //Accept and incoming connection
    puts("Waiting for incoming connections...");
    c = sizeof(struct sockaddr_in);

    while(1){
        //accept connection from an incoming client
        client_sock = accept(socketfd, (struct sockaddr *)&client, (socklen_t*)&c);
        if (client_sock < 0){
            cerr<<"accept failed"<<endl;
            exit(0);
        }
        puts("Connection accepted");

        int status;
        int nousepipe[PIPEFDN];           //把不要用的pipe紀錄起來，累績500個一次刪掉
        struct Num_pipe num_pipes[1001];  //創1001個，紀錄跳的pipe 

        InitEnv();
        char input[20000]="";
        //Receive a message from client
        while(write(client_sock, "% ", 2) && (read_size = readline(client_sock, input, 20000)) > 0){
            string cmdline = input;
            memset(input, 0, 20000); 
            replace(cmdline, "\n", "");

            dup2(client_sock, STDOUT_FILENO);
            dup2(client_sock, STDERR_FILENO);

            /*+++++++++++++++++++++++++++PIPE+++++++++++++++++++++++++++++++++++*/
            if(cmdline.size()==0) continue;
            while(waitpid(-1, NULL, WNOHANG)>0){}; //每一行指令前先檢查有沒有child proecess還沒執行完 
            // 最一開始就應該依照number pipe切割
            vector<string> S_C_B_N = split_cmdline_by_numpipe(cmdline+" ");//最後面加NULL
            for(int v=0; v<S_C_B_N.size(); v++)
            {
                bool write2file = false;
                int jump = 0;      
                int IN = 0, OUT = 0;
                int pipetype = -1;     // 0:'|', 1:'!'
                
                // 分割字符串，將command、argument填入表格
                vector<string> tokens = split(S_C_B_N[v], ' ');

                // 儲存command&argment
                vector<vector<string>> commands;     //2D-array儲存command
                vector<string> argments;             //1D-array儲存argment
                for(int i=0;i<tokens.size();i++)
                {
                    if(tokens[i][0]=='|' || tokens[i][0]=='!')
                    {
                        if((tokens[i].length())>1)
                        {
                            pipetype = (tokens[i][0]=='|' ? 0 : 1);
                            jump = stoi(tokens[i].substr(1,tokens[i].length()));
                            break;
                        }
                        else
                        {
                            commands.push_back(argments);  //push進commands
                            argments.clear();              //清空argments
                        }
                    }
                    else if(tokens[i] == ">")
                    {  //要寫進檔案
                        write2file=true;
                        commands.push_back(argments); //argments先push進argment
                        argments.clear();  //下一個argment存檔名，所以清空
                    }
                    else
                    {
                        argments.push_back(tokens[i]); //push進argments
                    }
                }
                commands.push_back(argments);   //最後的argment要放進commands


                // 實作 "setenv", "printenv", "exit"
                for(int i=0;i<commands.size();i++){
                    if(commands[i][0]=="setenv"){
                        setenv(commands[i][1].c_str(), commands[i][2].c_str(), 1); 
                        commands.clear(); //commands要清空!!!!
                    }else if(commands[i][0]=="printenv"){
                        char* env;
                        env = getenv(commands[i][1].c_str());
                        if(env){
                            write(client_sock, env, strlen(env));
                            write(client_sock, "\n", 1);
                        }
                        commands.clear(); //commands要清空!!!!
                    }
                    else if(commands[i][0]=="exit"){
                        close(client_sock);
                        commands.clear(); //commands要清空!!!!
                    }
                    break;
                }

                // number pipe，更新 count = count-1    
                for(int i = 0; i < 1000; i++){
                    if(num_pipes[i].count > 0) --num_pipes[i].count; 
                }

                /*~~~~~~~~~~~~~~~~~~~~~~~有 number pip~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
                if(pipetype > -1){
                    bool numpipe_exist = false;   //預設沒有開過 number pipe，判斷有沒有開過number pipe
                    for(int i = 0; i < 1000; i++){
                        if(num_pipes[i].count == jump){  
                            OUT = num_pipes[i].fd[1]; //OUT = number pipe的write 
                            numpipe_exist = true;     //number pipe
                            break;
                        }
                    }
                    if(numpipe_exist == false){   //沒有 開過number pipe
                        for(int i = 0; i < 1000; i++){
                            if(num_pipes[i].count == -1){
                                num_pipes[i].count = jump; //幾個指令後pipe
                                
                                if(pipe(num_pipes[i].fd) == -1){
                                    cerr << "Could not create pipe\n";
                                    break;
                                }else{
                                    OUT = num_pipes[i].fd[1]; //pipe的write的位置給OUT
                                }
                                break;  // 只做一次就停了
                            }
                        }
                    }  
                }
                // 如果有num pipe count=0的話，那麼一開始的IN就是num pipe，不是Stdin
                for(int i = 0; i < 1000; i++){
                    if(num_pipes[i].count == 0){   //如果count==0，那number pipe要執行
                        close(num_pipes[i].fd[1]); //把OUT關掉
                        IN = num_pipes[i].fd[0];
                        num_pipes[i].count = -1;
                        break;                   
                    }
                }
                
                int cmdcnt = commands.size()-1;
                /*******************************普通類型指令******************************/
                if(pipetype == -1){
                    int pipefd[2];  //read&write
                    pid_t pid;
                    for(int i = 0; i <= cmdcnt; i++){
                        char* exe_arg[10]={NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL};
                        for(int k=0;k<commands[i].size();k++){
                            exe_arg[k]=strdup(commands[i][k].c_str());
                        }
                        if(pipe(pipefd) == -1){  // 建立pipe 成功返回0，否則返回-1
                            cerr << "Could not create pipe\n";
                            break;
                        }
        
                        while((pid = fork()) < 0){   // fork失敗，父程序沒資源了，fork到成功為止
                            waitpid(-1, &status, 0); // 這此時waitpid()函數就完全退化成了wait()函數
                        }

                        if(pid == 0){ //子程序
                            // 寫檔案
                            if(write2file == true && i == cmdcnt){  // 3.6 寫入檔案一定是最後一個
                                int file;       // 開啟檔案後會回傳一個File Descriptor(檔案描述器)
                                if((file = open(exe_arg[0], FLAGS, mode)) == -1){
                                    cerr<<"Could not open file\n";
                                    break;
                                }

                                char buf[1024] = {0};
                                int readlen = 0;
                                while ( readlen = read(IN, buf, 1024)) { //從pipe讀取資料進buffer 
                                    write(file, buf, readlen);           //buffer寫資料進file
                                    memset(buf, 0, 1024);                //buffer歸零
                                }
                                close(pipefd[0]); 
                                close(pipefd[1]);
                                close(IN);
                                close(file);
                                exit(EXIT_SUCCESS);
                            }

                            // Stdin去IN讀資料
                            dup2(IN, STDIN_FILENO);  //第1個指令，如果都是(0,0)不改內容
                            if(i != cmdcnt) {   //最後一個指令Stdout不用往外接
                                dup2(pipefd[1], STDOUT_FILENO);
                            }            
                            close(pipefd[0]);
                            close(pipefd[1]);
                            // 子程序執行動作(ls,cat...) 把資料存進pipefd
                            execvp(exe_arg[0], exe_arg);
                            cerr << "Unknown command: [" << commands[i][0] << "].\n";
                            exit(EXIT_FAILURE);
                        }
                        else{ //父程序
                            // 每500個清理一次pipe buffer
                            if (i != 0 && i % PIPEFDN == 0) {
                                waitpid(pid, &status, WNOHANG);
                                for (int j = 1; j < PIPEFDN; j++) close(nousepipe[j]);
                            }
                            if (i > 0) nousepipe[i % PIPEFDN] = IN; //父程序會把每個pipe IN存起來，可以一次關掉
                            close(pipefd[1]);  
                            IN = pipefd[0];    //在父程序把資料(fd)連進IN
                        }
                    }

                    close(pipefd[0]);  // 父程序最後一步
                    close(IN);         // 最後把IN關掉
                    waitpid(pid, &status, 0);
                }
                
                /*~~~~~~~~~~~~~~~~~~~~~~~有 number pip指令~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/ 
                if(pipetype > -1){
                    int pipefd[2];
                    pid_t pid;
                    for(int i = 0; i <= cmdcnt; i++){
                        char* exe_arg[10]={NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL};
                        for(int k=0;k<commands[i].size();k++){
                            exe_arg[k]=strdup(commands[i][k].c_str());
                        }
                        
                        if(pipe(pipefd) == -1){  // 建立pipe 成功返回0，否則返回-1
                            cerr << "Could not create pipe\n";
                            break;
                        }
                            
                        while((pid = fork()) < 0){   // fork失敗，父程序沒資源了，fork到成功為止
                            waitpid(-1, &status, 0); // 這此時waitpid()函數就完全退化成了wait()函數
                        }

                        if (pid == 0){   //子程序
                            dup2(IN, STDIN_FILENO);              //一樣Stdin要讀IN的資料
                            if (i != cmdcnt) {                   //一樣Stdout存進pipefd[1]
                                dup2(pipefd[1], STDOUT_FILENO);
                            }
                            /* 差別 */
                            if(i == cmdcnt){                    //最後一個指令，一定是number pipe
                                dup2(OUT, STDOUT_FILENO);       //所以要把結果存進OUT

                                if(pipetype == 1){              //如果是!，
                                    dup2(OUT, STDERR_FILENO);   //就把Stderr也存進OUT
                                }
                            }

                            close(pipefd[0]);
                            close(pipefd[1]);
                            close(OUT);
                            // 執行動作(ls,cat...)
                            execvp(exe_arg[0], exe_arg);

                            cerr << "Unknown command: [" << commands[i][0] << "].\n";
                            exit(EXIT_FAILURE);
                        }else{ //父程序
                            if (i != 0 && i % PIPEFDN == 0) {
                                waitpid(pid, &status, WNOHANG);
                                for (int j = 1; j < PIPEFDN; j++) close(nousepipe[j]);
                            }
                            // IN
                            if (i > 0) nousepipe[i % PIPEFDN] = IN;
                            close(pipefd[1]);
                            IN = pipefd[0];
                        } 
                    }
                    close(pipefd[0]);
                    close(IN);
                    waitpid(pid, &status, 0);
                }
            }
        }
    }
    return 0;
}



const vector<string> split(const string &str, const char &delimiter){
    vector<string> result;
    stringstream ss(str);
    string tok;

    while (getline(ss, tok, delimiter)){
        result.push_back(tok);
    }
    return result;
}

vector<string> split_cmdline_by_numpipe(string cmdline){
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

bool replace(string& str, const string& from, const string& to) {
    size_t start_pos = str.find(from);
    if(start_pos == string::npos)
        return false;
    str.replace(start_pos, from.length(), to);
    return true;
}

void InitEnv(){
    setenv("PATH", "bin:.", 1);
}

int readline(int fd, char *ptr, int maxlen){
    int n, rc;
    char c;
    for (n = 1;n < maxlen;) {
        if ((rc=read(fd, &c, 1)) == 1){
            if (c != '\r' && c != '\n'){
                *ptr++ = c;
                n++;
            }
            if(c == '\n') break;

        }else if (rc == 0) {
            if (n == 1) return 0;
            else break;
        }else{
            return -1;
        } 
    }
    *ptr = 0;
    return n;
}