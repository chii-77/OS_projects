#include <stdio.h>
#include <stdlib.h>
#include <pthread.h> //for thread
#include <semaphore.h> //for信號機
#include <sys/time.h> //for gettimeofday()

void bubble_sort(int data[], int left, int right);
void merge_sorted(int data[], int left, int right, int mid);
void *worker();
void *dispatcher(void *arg);

/*讓每個thread都能存取*/
int *ori_arr;
int *arr;
volatile int stop_flag;
sem_t job_arrival; //信號機通知工作到來
sem_t job_done; //信號機通知工作完成
sem_t list_lock; //鎖住job list
sem_t table_lock; //鎖住job table

enum job_type{Job_Unassigned, Job_Assigned, Job_Done};

/*job preparation*/
struct job_node{ //job節點
    int index; //幫job編號
    char type; //bubble or merge
    int left, right, mid;
    enum job_type flag;
};
struct job_node job_table[16]; //紀錄job完成狀況(1-based)
struct job_list{
    int queue[32];
    int first;
    int last;
};
struct job_list list = {.first = 0, .last = 0};

/*create a new job*/
void create_job(int index, char type, int left, int right, int mid, enum job_type flag){
    job_table[index].index = index;
    job_table[index].type = type;
    job_table[index].left = left;
    job_table[index].mid = mid;
    job_table[index].right = right;
    job_table[index].flag = flag;
}

void add_job(struct job_list *list, int index){
    list->queue[list->last] = index;
    list->last++;
}

int job_delete(struct job_list *list){
    if(list->first == list->last){
        return -1;
    }
    return list->queue[list->first++];
}

/*sorts*/
void bubble_sort(int data[], int left, int right){
    if(right - left <= 1) return;
    for (int i=right-1; i>left; i--){
        for (int j=left; j<i; j++){
            if (data[j] > data[j+1]){
                int temp =data[j];
                data[j] = data[j+1];
                data[j+1] = temp;
            }
        }
    }
}

void merge_sorted(int data[], int left, int right, int mid){
    if(right - left <= 1) return;
    /*準備左右陣列*/
    int n1 = mid - left; //左陣列長度
    int n2 = right - mid;
    int L[n1], R[n2];
    for(int i=0; i<n1; i++){
        L[i] = data[left + i];
    }
    for(int i=0; i<n2; i++){
        R[i] = data[mid + i];
    }

    /*合併*/
    int i=0, j=0, k=left;
    while(i<n1 && j<n2){
        data[k++] = (L[i] < R[j]) ? L[i++]:R[j++];
    }
    while(i<n1) data[k++] = L[i++];
    while(j<n2) data[k++] = R[j++];
}

void *worker(){
    while(!stop_flag){
        sem_wait(&job_arrival); //wait for job
        if(stop_flag) {
            sem_post(&job_arrival);
            break;
        };

        sem_wait(&list_lock); //lock the list
        int job = job_delete(&list); //接住回傳的第一個job
        sem_post(&list_lock); //unlock the list
        if (job == -1) continue; //job list is empty

        /*sort*/
        if (job_table[job].type == 'b'){
            bubble_sort(arr, job_table[job].left, job_table[job].right);
        }else{
            merge_sorted(arr, job_table[job].left, job_table[job].right, job_table[job].mid);
        }
        sem_wait(&table_lock); //lock the table
        job_table[job].flag = Job_Done;
        sem_post(&table_lock); //unlock the table
        sem_post(&job_done); //signal to dispatcher
    }
    pthread_exit(NULL);
}

void *dispatcher(void *arg){
    /*initial 8 jobs*/
    int len = *(int*)arg; //取出整數值
    int bubble_size = len / 8;
    for (int i = 0; i < 8; i++){
        int left = i * bubble_size;
        int right = (i == 7) ? len : left + bubble_size;
        sem_wait(&table_lock); //lock the table
        create_job(i+8, 'b', left, right, 0, Job_Assigned); //維護8~15的job table
        sem_post(&table_lock); //unlock the table

        sem_wait(&list_lock); //lock the list
        add_job(&list, i+8); //工作加入list
        sem_post(&list_lock); //unlock the list

        sem_post(&job_arrival); //signal to worker
    }

    /*有worker完成工作，再分配*/
    while(!stop_flag){
        sem_wait(&job_done);

        sem_wait(&table_lock); //lock the table
        if(job_table[1].flag == Job_Done){ //根節點完成
            stop_flag = 1;
            sem_post(&table_lock); //unlock the table
            sem_post(&job_arrival); //signal to worker
            pthread_exit(NULL);
        } 
        /*真的分新工作*/
        for(int i = 2; i < 15; i+=2){
            if(job_table[i].flag == Job_Done && job_table[i+1].flag == Job_Done && job_table[i/2].flag == Job_Unassigned){ //相鄰兩個都完成且父節點未完成
                create_job(i/2, 'm', job_table[i].left, job_table[i+1].right, job_table[i].right, Job_Assigned);
                
                sem_wait(&list_lock); //lock the list
                add_job(&list, i/2);
                sem_post(&list_lock); //unlock the list
                sem_post(&job_arrival); //signal to worker
            }
        }
        sem_post(&table_lock); //unlock the table
    }
    pthread_exit(NULL); //通知 main dispatcher結束
}

int main(){
    FILE *file;
    int n;
    struct timeval start, end; //for gettimeofday()
    
    /*get input*/
    file = fopen("input.txt", "r");
    fscanf(file, "%d", &n);
    ori_arr = malloc(n * sizeof(int));
    arr = malloc(n * sizeof(int));
    for (int i = 0; i < n; i++){
        fscanf(file, "%d", &ori_arr[i]);
    }
    fclose(file);

    for(int worker_num = 1; worker_num <=8; worker_num++){
        gettimeofday(&start, NULL);

        /*initialize*/
        sem_init(&job_arrival, 0, 0);
        sem_init(&job_done, 0, 0);
        sem_init(&list_lock, 0, 1);
        sem_init(&table_lock, 0, 1);
        list.first = 0;
        list.last = 0;
        stop_flag = 0;
        for(int i = 0; i < 16; i++){
            job_table[i].flag = Job_Unassigned;
        }
        for (int i = 0; i < n; i++){
            arr[i] = ori_arr[i];
        }

        /*實際執行*/
        /*create dispatcher*/
        pthread_t d_thread;
        pthread_attr_t attr;
        pthread_attr_init(&attr); //初始化thread屬性
        pthread_create(&d_thread, &attr, dispatcher, &n);

        /*create worker*/
        pthread_t w_thread[worker_num];
        for(int i = 0; i < worker_num; i++){
            pthread_create(&w_thread[i], &attr, worker, NULL);
        }
        pthread_join(d_thread, NULL);
        for(int i = 0; i < worker_num; i++){
            pthread_join(w_thread[i], NULL);
        }
        gettimeofday(&end, NULL);
        float sec = end.tv_sec - start.tv_sec;
        float usec = end.tv_usec - start.tv_usec;
        printf("worker thread #%d, elapsed %0.6f ms\n", worker_num, sec * 1000 + usec / 1000);
        /*print result*/
        char filename[32];
        sprintf(filename, "output_%d.txt", worker_num);
        file = fopen(filename, "w");
        for (int i = 0; i < n; i++){
            fprintf(file, "%d ", arr[i]);
        }
        fclose(file);

        /*free*/
        sem_destroy(&job_arrival);
        sem_destroy(&job_done);
        sem_destroy(&list_lock);
        sem_destroy(&table_lock);
    }
    free(ori_arr);
    free(arr);
    return 0;
}