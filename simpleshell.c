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
        printf("Background process with PID %d terminated\n", wpid);
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
        bool background = false; //是否在背景執行
        bool redirect_output = false; //是否重導向輸出
        bool redirect_input = false; //是否重導向輸入
        char *file = NULL; //指定重導向檔案

        while (token != NULL) {
            args[i++] = token; //第一次的token先存
            token = strtok(NULL, delim);
        }
        args[i] = NULL; //最後一個元素設為NULL，表示結束

        /* 檢查Segmentation fault (core dumped)問題
        printf("args before execvp:\n");
        for(int j = 0; args[j] != NULL; j++){
            printf("args[%d] = %s\n", j, args[j]);
        }
        printf("redirect file = %s\n", file);
        */

        last_index = i - 1;
        if (last_index >= 0 && strcmp(args[last_index], "&") == 0){ //strcmp比較字串是否相等
            background = true;
            args[last_index] = NULL; //已經處理完＆
        }

        /*deal with redirect*/
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
        }

        /*fork a child process*/
        pid_t pid, wpid;
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
            execvp(args[0], args);
            perror("execvp"); //顯示錯誤資訊
            exit(1);    // 異常結束
        }else if(pid > 0){ //父程序
            if (background){
                printf("Background process started with PID: %d\n", pid);
            }else{    
                waitpid(pid, &wpid, 0); //等待子程序完成，子程序結果存入wpid
            }
        }else{
            printf("fork failed");
            exit(1);
        }
    
    }
    return 0;
}
    