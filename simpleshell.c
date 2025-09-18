#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> //system calls
#include <sys/wait.h> //wait
#include <string.h> //strtok
#include <stdbool.h>



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
       

        while (token != NULL) {
            args[i++] = token; //第一次的token先存
            token = strtok(NULL, delim);
        }
        args[i] = NULL; //最後一個元素設為NULL，表示結束
        


        /*fork a child process*/
        pid_t pid, wpid;
        pid = fork();
        if(pid == 0){ //子程序，執行外部命令
            /*execute the command*/
            execvp(args[0], args);
            perror("execvp"); //顯示錯誤資訊
            exit(1);    // 異常結束
        }else if(pid > 0){ //父程序
                waitpid(pid, &wpid, 0); //等待子程序完成，子程序結果存入wpid
        }else{
            printf("fork failed");
            exit(1);
        }
    
    }
    return 0;
}
    