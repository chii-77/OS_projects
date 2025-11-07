#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
//#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#define HEADER_SIZE 32

/*建立header*/
struct chunk{
    size_t payload_size; // chunk可用的大小
    int is_free; // 0 is false, 1 is true
    int padding; //預留空位
    struct chunk *next;
    struct chunk *prev;
};
struct chunk* first;

size_t round_up(size_t size){
    return (size % 32 == 0) ? size : ((size/32) + 1) * 32;
}

size_t IntToString(int num, char *str){
    char *p = str;
    unsigned x = num;
    if (num < 0) {x = -x;} //負數轉正數
    do{
        *p++ = '0' + x % 10;
        x /= 10;
    }while(x > 0); //把數字轉換成字串
    if (num < 0) {*p++ = '-';}

    size_t len = p - str;
    for (char* i = str, *j = str + len - 1; i < j; i++, j--){
        char temp = *i;
        *i = *j;
        *j = temp;
    }
    return len;
}

void init(){
    first = mmap(NULL, 20000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    first->payload_size = 20000 - HEADER_SIZE;
    first->is_free = 1;
    first->padding = 0;
    first->next = NULL;
    first->prev = NULL;
}

void split_chunk(struct chunk *p, size_t rounded_size){
    if(p->payload_size <= rounded_size + HEADER_SIZE) return; //free chunk不夠新的chunk無法分割，直接使用
    struct chunk* new_chunk = (struct chunk*)((char*)p + HEADER_SIZE + rounded_size);
    new_chunk->payload_size = p->payload_size - rounded_size - HEADER_SIZE;
    new_chunk->is_free = 1;
    new_chunk->padding = 0;
    new_chunk->next = p->next;
    new_chunk->prev = p;
    if (p->next != NULL) p->next->prev = new_chunk;
    p->next = new_chunk;
    p->payload_size = rounded_size;
}

int is_malloc = 0;
void* malloc(size_t size){
    struct chunk *p;
    struct chunk *best_fit_chunk = NULL;
    size_t n;
    size_t best_fit_size;
    size_t rounded_size = round_up(size);
    /*如果第一次malloc，則初始化*/
    if(is_malloc == 0){
        is_malloc = 1;
        init();
    }
    best_fit_size = 20000;
    
    /*如果size為0，則回傳最大的free chunk*/
    if(size == 0){
        size_t max_free_size = 0;
        char temp[10];
        for(p = first; p!= NULL; p=p->next){
            if(p->is_free == 1 && p->payload_size > max_free_size){
                max_free_size = p->payload_size;
            }
        }
        n = IntToString(max_free_size, temp); //把最大free chunk轉換成字串
        write(1, "Max Free Chunk Size = ", 22);
        write(1, temp, n);
        write(1, "\n", 1);
        munmap(first, 20000);
        is_malloc = 0;
        first = NULL;
        return NULL;
    }

    /*尋找合適的free chunk*/
    for(p = first; p!= NULL; p=p->next){
        if(p->is_free == 1){
            if(p->payload_size >= rounded_size && p->payload_size < best_fit_size){
                best_fit_size = p->payload_size;
                best_fit_chunk = p;
            }
        }
    }
    if (best_fit_chunk == NULL) return NULL;

    split_chunk(best_fit_chunk, rounded_size);
    best_fit_chunk->is_free = 0; //把chunk設為已使用
    return (void*)((char*)best_fit_chunk + HEADER_SIZE);
}

void free(void *ptr){
    /*建立free chunk的結構*/
    struct chunk *p = (struct chunk*)ptr - 1;
    struct chunk *left = p->prev;
    struct chunk *right = p->next;

    /*開始合併chunk*/
    if(left == NULL){ //左邊沒有chunk
        if(right && right->is_free == 1){ //右邊有可以合併的chunk
            p->payload_size = p->payload_size + HEADER_SIZE + right->payload_size;
            p->next = right->next;
            if (right->next != NULL) right->next->prev = p;
        }
        p->is_free = 1; //無論是否合併，本來要free的chunk都要設為free
    }
    else if(right == NULL){ //右邊沒有chunk
        if(left && left->is_free == 1){ //左邊有可以合併的chunk
            left->payload_size = left->payload_size + HEADER_SIZE + p->payload_size;
            left->next = right;
        }
        p->is_free = 1; //無論是否合併，本來要free的chunk都要設為free
    }
    else{ //左右都有chunk
        if(right->is_free == 1 && left->is_free == 1){ //右邊和左邊都有可以合併的chunk
            left->payload_size = left->payload_size + HEADER_SIZE + p->payload_size + HEADER_SIZE + right->payload_size;
            left->next = right->next;
            if (right->next != NULL) right->next->prev = left;
            left->is_free = 1;
        }
        else if(right->is_free == 1){ //只有右邊有可以合併的chunk
            p->payload_size = p->payload_size + HEADER_SIZE + right->payload_size;
            p->next = right->next;
            if (right->next != NULL) right->next->prev = p;
            p->is_free = 1;
        }
        else if(left->is_free == 1){ //只有左邊有可以合併的chunk
            left->payload_size = left->payload_size + HEADER_SIZE + p->payload_size;
            left->next = right;
            right->prev = left;
            p->is_free = 1;
        }
        else{ //左右都沒有可以合併的chunk
            p->is_free = 1;
        }
    }
}