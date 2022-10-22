/* 只能處理簡單pipe，可以過2個測資*/
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
using namespace std;

#define FLAGS O_RDWR|O_CREAT|O_TRUNC  // O_RDWR: 以讀寫模式開啟,O_CREAT: 如果指定的檔案不存在，就建立一個,O_TRUNC: 若開啟的檔案存在且是一般檔案，開啟後就將其截短成長度為 0 
#define mode 0666     // mode: Unix 權限位元設定

const vector<string> split(const string &str, const char &delimiter) 
{
    vector<string> result;
    stringstream ss(str);
    string tok;

    while (getline(ss, tok, delimiter)) {
        result.push_back(tok);
    }
    return result;
}


void InitEnv()
{
    setenv("PATH", "bin:.", 1);
}


int main(){
    string cmdline;
    vector<int> jumps;
    vector<vector<string>>jump_commands;
    InitEnv();
    while(cout<<"% " && getline(cin, cmdline)){
        
        // 1. 看有幾個"|"就創幾個pipe  
        int pipe_num=0;
        for(int i=0; i<cmdline.length(); i++){
            if(cmdline[i]=='|' && cmdline[i+1]==' ') pipe_num++;
        }
        int pipe_fds[pipe_num*2]; // 需要共 pipe_num 條 pipe  
        for (int i=0; i<pipe_num; i++) pipe(pipe_fds + i*2); // 建立 i-th pipe

        //cout<<"共創了"<<pipe_num<<"個pipe_fds"<<endl;
        
        // 2. 切割cmdline
        vector<string> tokens = split(cmdline, ' ');
        
        // 3. 儲存command&argment
        vector<vector<string>> commands;     //2D-array儲存command
        vector<string> argments;             //1D-array儲存argment
        bool write2file = false; 
        string filename;
        for(int i=0;i<tokens.size();i++){
            if(tokens[i][0]=='|' || tokens[i][0]=='!'){
                if((tokens[i].length())>1){
                    int jump = stoi(tokens[i].substr(1,tokens[i].length()));
                    jumps.push_back(jump);  
                    jump_commands.push_back(argments); //前一個指令(還在argments)要放入jump_commands
                    argments.clear();              //清空argments
                }else{
                    commands.push_back(argments);  //push進commands
                    argments.clear();              //清空argments
                }
            }
            else if(tokens[i] == ">"){  //要寫進檔案
                write2file=true;
                commands.push_back(argments); //argments先push進argment
                argments.clear();  //下一個argment存檔名，所以清空
            }
            else{
                argments.push_back(tokens[i]); //push進argments
            }
        }
        if(write2file) filename=argments[0];
        else commands.push_back(argments);   //最後的argment要放進commands


        // 4. 特殊字元處理'setenv','printenv','exit'
        for(int i=0;i<commands.size();i++){
            if(commands[i][0]=="setenv"){
                setenv(commands[i][1].c_str(), commands[i][2].c_str(), 1); 
                commands.clear(); //commands要清空!!!!
            }else if(commands[i][0]=="printenv"){
                char* env;
                env = getenv(commands[i][1].c_str());
                if(env) cout<<env<<endl;
                commands.clear(); //commands要清空!!!!
            }else if(commands[i][0]=="exit"){
                exit(EXIT_SUCCESS);
            }
        }
        

        // 5. pipe處理
        //cout<<"commands.size()="<<commands.size()<<endl;
        pid_t pid;
        for(int i=0;i<commands.size();i++){ // loop will run size of commands times
            char* exe_arg[10]={NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL};
            for(int k=0;k<commands[i].size();k++){
                exe_arg[k]=strdup(commands[i][k].c_str());
            }
            
            // 開始fork child 來執行指令
            //cout<<"i= "<<i<<endl;
            pid = fork();
            if(pid<0){
                cerr << "Fork failure"<<endl;
                exit(EXIT_FAILURE);
            
            }else if(pid == 0){ //child
                
                //第1個pipe
                if(i == 0){
                    //cout<<"一開始執行= "<<exe_arg[0]<<endl; 
                    close(pipe_fds[0]);
                    dup2(pipe_fds[1], STDOUT_FILENO);
                    //全關
                    for(int e = 0; e < (pipe_num*2); e++){
                        close(pipe_fds[e]);
                    }
                    //如果有file
                    if(write2file && i==(commands.size() - 1)){
                        int fd = open(filename.c_str(), FLAGS, mode);
                        dup2(fd, STDOUT_FILENO); //當下的std
                    }
                    
                    execvp(exe_arg[0], exe_arg);
                    cerr << "Unknown command: [" << exe_arg[0] << "].\n";
                    exit(EXIT_FAILURE);
                }
                //第2~commandsize-2
                else if(i > 0 && i < (commands.size() - 1)){
                    //cout<<"中間執行= "<<exe_arg[0]<<endl;
                    close(pipe_fds[i * 2 - 1]);
                    dup2(pipe_fds[(i - 1) * 2], STDIN_FILENO);
                    close(pipe_fds[i * 2]);
                    dup2(pipe_fds[i * 2 + 1], STDOUT_FILENO);
                    
                    //全關
                    for(int e = 0; e < (pipe_num*2); e++){
                        close(pipe_fds[e]);
                    }
                    
                    execvp(exe_arg[0], exe_arg);
                    cerr << "Unknown command: [" << exe_arg[0] << "].\n";
                    exit(EXIT_FAILURE);
                }

                // 最後一個
                else{
                    //cout<<"最後執行= "<<exe_arg[0]<<endl;
                    close(pipe_fds[i * 2 - 1]);
                    dup2(pipe_fds[(i - 1) * 2], STDIN_FILENO);
                    //全關
                    for(int e = 0; e < (pipe_num*2); e++){
                        close(pipe_fds[e]);
                    }
                    //如果有file
                    if(write2file){
                        int fd = open(filename.c_str(), FLAGS, mode);
                        dup2(fd, STDOUT_FILENO); //當下的std
                    }
                    
                    execvp(exe_arg[0], exe_arg);
                    cerr << "Unknown command: [" << exe_arg[0] << "].\n";
                    exit(EXIT_FAILURE);
                    }
            }
            else { // Parent
                //printf("- fork %d\n", pid);
                // 全關
                if (i != 0){
                    close(pipe_fds[(i - 1) * 2]);     
                    close(pipe_fds[(i - 1) * 2 + 1]); 
                }
            } 
            waitpid(pid, NULL, 0); // 等最後一個指令結束
        }

        //cout << "All done." << endl;
        //commands.clear(); // 最後commands要清空!!!!   
    }
}
