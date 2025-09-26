//以System V實作平行矩陣運算
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h> //for uint32_t
#include <sys/shm.h> //for shared memory
#include <sys/ipc.h> //for IPC
#include <sys/wait.h> //for wait()
#include <sys/time.h> //for gettimeofday()

int main(){
    int dim;
    int shm_id;
    struct timeval start, end; //for gettimeofday()

    /*get input*/
    printf("Input the matrix dimension: ");
    scanf("%d", &dim);  //讀取矩陣維度
    printf("\n");
    size_t shm_size= dim * dim * sizeof(uint32_t); //計算共享記憶體大小只存放矩陣C

    /*initialize matrix*/
    uint32_t matrix_ab[dim][dim]; //使用uint32_t型態
    for (int i = 0; i < dim; i++){
        for (int j = 0; j < dim; j++){
            matrix_ab[i][j] =  i * dim + j;
        }
    }

    /*parallel processing*/
    for (int parallel_num = 1; parallel_num <= 16; parallel_num++){ //1到16個process
        int total_row = dim / parallel_num;
        /*create shared memory*/
        shm_id = shmget(IPC_PRIVATE, shm_size, IPC_CREAT | 0666);
        if (shm_id == -1){
            perror("shmget failed");
            exit(1);
        }

        gettimeofday(&start, NULL);
        for (int process_num = 1; process_num <= parallel_num; process_num++){ //create process
            int row_start = (process_num - 1) * total_row;
            int row_end = (process_num == parallel_num) ? dim : row_start + total_row;
            
            pid_t pid = fork();
            if (pid == -1){
                perror("fork failed");
                exit(1);
            }else if (pid == 0){
                /*child process*/
                uint32_t *matrix_c = (uint32_t *)shmat(shm_id, NULL, 0); //attach
                if (matrix_c == (uint32_t *)-1){
                    perror("shmat failed");
                    exit(1);
                }
                uint32_t (*matrix_cc)[dim] = (uint32_t (*)[dim])matrix_c; //轉換為二維陣列

                /*矩陣乘法*/
                for (int p = row_start; p < row_end; p++){
                    for (int q = 0; q < dim; q++){
                        matrix_cc[p][q] = 0;
                        for (int r = 0; r < dim; r++){
                            matrix_cc[p][q] += matrix_ab[p][r] * matrix_ab[r][q];
                        }
                    }
                }
                shmdt(matrix_c); //detach
                exit(0);
            }
        } 
        while (wait(NULL) > 0); //父程序等子程序完成
        gettimeofday(&end, NULL);

        /*calculate checksum*/
        int sec = end.tv_sec - start.tv_sec;
        int usec = end.tv_usec - start.tv_usec;
        uint32_t checksum = 0;
        uint32_t *matrix_c = (uint32_t *)shmat(shm_id, NULL, 0); //再連接一次計算checksum
        uint32_t (*matrix_cc)[dim] = (uint32_t (*)[dim])matrix_c; //轉換為二維陣列
        for (int i = 0; i < dim; i++){
            for (int j = 0; j < dim; j++){
                checksum += matrix_cc[i][j];
            }
        }
        printf("Multiplying matrixes using %d processes\n", parallel_num);
        printf("Elapsed time: %f sec, Checksum: %u\n", sec+(usec/1000000.0), checksum);
        
        shmdt(matrix_c); //detach
        shmctl(shm_id, IPC_RMID, NULL); //清除共享記憶體
    }
    return 0;
}