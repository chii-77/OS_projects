#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
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

/*Multiple Level Free List*/
enum {NUM_LEVEL = 11}; //11個level
struct free_node{ //free list的節點
    struct chunk* blk;
    struct free_node* next;
};
struct free_list{ //free list的鏈結串列
    struct free_node* first;
    struct free_node* last;
};
struct free_list free_lists[NUM_LEVEL]; 

struct free_node *node_pool = NULL;
size_t node_cap = 0, node_used = 0;

/*建立node pool*/
void node_pool_alloc(size_t need){
    if(node_pool) return; //如果node_pool已經存在，則不重新分配
    size_t new_cap = need ? need : 4096; //初始化node_cap為4096
    struct free_node *np = mmap(NULL, new_cap * sizeof(struct free_node), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if(np == MAP_FAILED){
        exit(1);
    }
    node_pool = np;
    node_cap = new_cap;
    node_used = 0;
}

/*建立新的free node*/
struct free_node *node_new(struct chunk *p){
    if(!node_pool) node_pool_alloc(4096);
    if(node_used >= node_cap) _exit(1);
    struct free_node *n = &node_pool[node_used++];
    n->blk = p;
    n->next = NULL;
    return n;
}

/*映射*/
static inline int list_index(size_t sz){
    size_t cap = 32;
    int i = 0;
    while(cap < sz && i < NUM_LEVEL - 1){
        cap <<= 1;
        i++;
    }
    return i;
}

/*初始化LEVel list與node pool*/
void level_init(){
    for(int i = 0; i < NUM_LEVEL; i++){
        free_lists[i].first = NULL;
        free_lists[i].last = NULL;
    }
    if (!node_pool){
        node_pool_alloc(1024);
    }
}

void list_push(struct chunk *p){
    int idx = list_index(p->payload_size);
    struct free_node *n = node_new(p);
    if(free_lists[idx].first == NULL){
        free_lists[idx].first = n;
        free_lists[idx].last = n;
    }
    else{
        free_lists[idx].last->next = n;
        free_lists[idx].last = n;
    }
    p->is_free = 1;
}

void remove_neighbor(struct chunk *p){
    int idx = list_index(p->payload_size);
    struct free_node *prev = NULL;
    struct free_node *curr = free_lists[idx].first;
    while(curr != NULL){
        if(curr->blk == p){
            if(prev){
                prev->next = curr->next;
            }
            else{
                free_lists[idx].first = curr->next;
            }
            if(!curr->next){
                free_lists[idx].last = prev;
            }
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

/*從level list中pop出合適的free chunk*/
struct chunk *list_pop(size_t sz){
    int idx = list_index(sz);
    for (idx ; idx < NUM_LEVEL; idx++){
        struct free_node *prev = NULL;
        struct free_node *curr = free_lists[idx].first;
        size_t best_size = (size_t)-1; //初始化best_size為最大值
        struct free_node *best_node = NULL;
        struct free_node *best_prev = NULL;

        while(curr != NULL){
            size_t s = curr->blk->payload_size;
            if(s >= sz){ //找到合適的free chunk
                if(s < best_size){
                    best_size = s;
                    best_node = curr;
                    best_prev = prev;
                }
            }
            prev = curr;
            curr = curr->next;
        }

        if(best_node){
            if(best_prev){
                best_prev->next = best_node->next;
            }
            else{
                free_lists[idx].first = best_node->next;
            }
            if(!best_node->next){
                free_lists[idx].last = best_prev;
            }
            return best_node->blk;
        }
        /*沒找到合適的free chunk，往下一個level找*/
    }
    return NULL; //所有level都沒找到合適的free chunk，回傳NULL
}

void init(){
    first = mmap(NULL, 20000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    first->payload_size = 20000 - HEADER_SIZE;
    first->is_free = 1;
    first->padding = 0;
    first->next = NULL;
    first->prev = NULL;

    level_init();
    list_push(first);
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

    list_push(new_chunk); //把新的chunk加入level list
}

int is_malloc = 0;
void* malloc(size_t size){
    struct chunk *p;
    size_t n;
    size_t rounded_size = round_up(size);
    /*如果第一次malloc，則初始化*/
    if(is_malloc == 0){
        is_malloc = 1;
        init();
    }
    
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

    p = list_pop(rounded_size);
    if(!p){
        return NULL;
    }
    split_chunk(p, rounded_size);
    p->is_free = 0; //把chunk設為已使用
    return (void*)((char*)p + HEADER_SIZE);
}

void free(void *ptr){
    /*建立free chunk的結構*/
    struct chunk *p = (struct chunk*)ptr - 1;
    struct chunk *left = p->prev;
    struct chunk *right = p->next;

    /*開始合併chunk*/
    if(right && right->is_free == 1){
        remove_neighbor(right);
        p->payload_size += right->payload_size + HEADER_SIZE;
        p->next = right->next;
        if(p->next != NULL){
            p->next->prev = p;
        }
    }
    if(left && left->is_free == 1){
        remove_neighbor(left);
        left->payload_size += p->payload_size + HEADER_SIZE;
        left->next = p->next;
        if(left->next != NULL){
            left->next->prev = left;
        }
        p = left;
    }
    p->is_free = 1;
    list_push(p);
}