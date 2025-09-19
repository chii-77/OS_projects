#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> //system calls
#include <sys/wait.h> //wait
#include <string.h> //strtok
#include <stdbool.h>
#include <signal.h> //signal

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

        /*fork a child process*/
        pid_t pid, wpid;
        pid = fork();
        if(pid == 0){ //子程序，執行外部命令
            /*execute the command*/
            if(background){
                signal(SIGINT, SIG_IGN); //忽略SIGINT信號
            }
            execvp(args[0], args);
            perror("execvp"); //顯示錯誤資訊
            exit(1);    // 異常結束
        }else if(pid > 0){ //父程序
            if (background){
                printf("Background process started with PID: %d\n", pid);
                signal(SIGCHLD, func);
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
    