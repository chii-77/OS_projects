#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> //system calls
#include <sys/wait.h> //wait
#include <string.h> //strtok
#include <stdbool.h>
#include <signal.h> //signal
#include <fcntl.h> //open

/*deal with background process terminated*/
void func(int signum)
{
    int status;
    pid_t wpid;

    while((wpid = waitpid(-1, &status, WNOHANG)) > 0){
    }
}

int main(){
    signal(SIGCHLD, func); //處理背景程序結束
    
    while(true){
        /*get input from user*/
        printf("%c", '>');
        char input[1024]; //靜態分配

        /*read input from user*/
        fgets(input, 1024, stdin); //將輸入直接放入input陣列中
        if(input != NULL){
            input[strcspn(input, "\n")] = '\0'; //將輸入的換行符號去掉
            // printf("You entered: %s\n", input);
        }

        /*parse the input*/
        char *delim = " "; 
        char *token  = strtok(input, delim);
        char *args[1024];
        int i = 0;
        int last_index = 0; //追蹤最後一個參數
        int status;
        bool background = false; //是否在背景執行
        bool redirect_output = false; //是否重導向輸出
        bool redirect_input = false; //是否重導向輸入
        char *file = NULL; //指定重導向檔案
        bool pipe_cmd = false; //是否使用管道
        char *args1[1024]; //管道左邊的參數
        char *args2[1024]; //管道右邊的參數
        int fd[2]; //管道

        while (token != NULL) {
            args[i++] = token; //第一次的token先存
            token = strtok(NULL, delim);
        }
        args[i] = NULL; //最後一個元素設為NULL，表示結束

        last_index = i - 1;
        if (last_index >= 0 && strcmp(args[last_index], "&") == 0){ //strcmp比較字串是否相等
            background = true;
            args[last_index] = NULL; //已經處理完＆
        }

        /*deal with special characters*/
        for(int x = 0; args[x] != NULL; ++x){
            if (strcmp(args[x], ">") == 0){
                redirect_output = true;
                file = args[x+1]; //指定重導向檔案
                args[x] = NULL; //將符號移除
                break;
            }
            if (strcmp(args[x], "<") == 0){
                redirect_input = true;
                file = args[x+1];
                args[x] = NULL;
                break;
            }
            if (strcmp(args[x], "|") == 0){
                pipe_cmd = true;
                pipe(fd); //建立管道，fd[0]是讀取端，fd[1]是寫入端
                int y = 0; //參數索引
                for(y = 0; y < x; y++){
                    args1[y] = args[y];
                }
                args1[y] = NULL;
                for(y = x+1; args[y] != NULL; y++){
                    args2[y-x-1] = args[y];
                }
                args2[y-x-1] = NULL;
                break;
            }
        }

        /*fork a child process*/
        pid_t pid, pid2;
        pid = fork();
        if(pid == 0){ //子程序，執行外部命令
            /*execute the command*/
            if(background){
                signal(SIGINT, SIG_IGN); //忽略SIGINT信號
            }
            if(redirect_output){
                int f = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if(f == -1){
                    perror("open");
                    exit(1);
                }
                dup2(f, 1);
                close(f);
            }
            if(redirect_input){
                int f = open(file, O_RDONLY);
                if(f == -1){
                    perror("open");
                    exit(1);
                }
                dup2(f, 0); //用0指到檔案
                close(f);
            }
            if(pipe_cmd){
                dup2(fd[1], 1); //用1指到管道
                close(fd[0]); //關閉讀取端
                close(fd[1]); //關閉寫入端，由1代替
                execvp(args1[0], args1); //執行管道左邊的命令
                perror("execvp"); //顯示錯誤資訊
                exit(1);    // 異常結束
            }
            execvp(args[0], args);
            perror("execvp"); //顯示錯誤資訊
            exit(1);    // 異常結束
        }else if(pid > 0){ //父程序
            if(pipe_cmd){ 
                pid2 = fork(); //建立另一個子程序
                if(pid2 == 0){ //子程序，執行外部命令
                    dup2(fd[0], 0); //用0指到管道
                    close(fd[0]); //關閉讀取端
                    close(fd[1]); //關閉寫入端，由0代替
                    execvp(args2[0], args2); //執行管道右邊的命令
                    perror("execvp"); //顯示錯誤資訊
                    exit(1);    // 異常結束
                }else{ //父程序關閉pipe
                    close(fd[0]);
                    close(fd[1]);
                    waitpid(pid, &status, 0);
                    waitpid(pid2, &status, 0);
                }
            }else if (background){
                printf("Background process started with PID: %d\n", pid);
            }else{ //沒有背景執行且沒有管道
                waitpid(pid, &status, 0); //等待子程序完成，子程序結果存入status
            }
        }else{
            printf("fork failed");
            exit(1);
        }
    }
    return 0;
}
    