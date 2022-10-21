# Network Programming Project 1 - NPShell


###### tags: Network Programming、pipe、dup2

## Pipe 架構規劃
### **Normal Pipe**
![](https://i.imgur.com/iI4GsVF.png)

---

### **Number Pipe**
* Step1
![](https://i.imgur.com/qWKTgfD.png)
* Step2
![](https://i.imgur.com/yKyuD5B.png)

---


### 將輸入字串依照Number Pipe分割
以下兩種輸出結果相同
> $ removetag test.html |2 removetag test.html |1 
> $ number |1 number
 
> $ removetag test.html |2
> $ removetag test.html |1
> $ number |1
> $ number



```c++=1
vector<string> split_cmdline_by_numpipe(string cmdline){
    bool isnumpipe;
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
    

    return tempcommands;
}
```

---
### 使用2-D儲存command、argment

> 表格如下列情況
>
> | command  | argment  |
> | -------- | -------- | 
> | ls       | -l       |
> | number   |          |
> | removetag|test.html | 


```c++=1
// 儲存command&argment
vector<vector<string>> commands;     //2D-array儲存command
vector<string> argments;             //1D-array儲存argment
for(int i=0;i<tokens.size();i++){
    if(tokens[i][0]=='|' || tokens[i][0]=='!'){
        if((tokens[i].length())>1){
            pipetype = (tokens[i][0]=='|' ? 0 : 1);
            jump = stoi(tokens[i].substr(1,tokens[i].length()));
            break;
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
commands.push_back(argments);   //最後的argment要放進commands

```