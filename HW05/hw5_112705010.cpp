#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <string>
#include <sys/time.h> // for timer
using namespace std;

static uint64_t PAGE_SIZE = 4096;

//trace record
struct Trace{
    bool is_write; //0 for read, 1 for write
    uint64_t page; //page number
};

//
enum RegionType{
    REGION_WORK = 0,
    REGION_CLEAN = 1
};

struct Node {
    uint64_t page;
    bool dirty;
    RegionType region;

    //region list (working/clean-first)
    Node* rprev;
    Node* rnext;

    //clean list(只放在clean list)
    Node* cprev;
    Node* cnext;

    Node(uint64_t p, bool d)
        : page(p), dirty(d), region(REGION_WORK), rprev(nullptr), rnext(nullptr), cprev(nullptr), cnext(nullptr) {}
};

//region list (用rprev/ rnext)
struct RegionList{
    Node* head = nullptr; //MRU
    Node* tail = nullptr; //LRU
    size_t size = 0;

    void clear(){
        head = tail = nullptr;
        size = 0;
    }

    //move node to front of the list
    void move_to_front(Node* n){
        if (!n || n == head) return;

        //detach
        if (n->rprev) n->rprev->rnext = n->rnext; //原本前面有東西，將他的next指向n的next
        if (n->rnext) n->rnext->rprev = n->rprev;
        if (n == tail) tail = n->rprev;
        
        //attach to front
        n->rprev = nullptr;
        n->rnext = head;
        if (head) head->rprev = n;
        head = n;
        if (!tail) tail = head;
    }

    //push node to front of the list
    void push_front(Node* n){
        n->rprev = nullptr;
        n->rnext = head;
        if (head) head->rprev = n;
        head = n;
        if (!tail) tail = head;
        ++size;
    }

    //pop node from front of the list
    void remove(Node* n){
        if (!n) return;
        if (n->rprev) n->rprev->rnext = n->rnext;
        if (n->rnext) n->rnext->rprev = n->rprev;
        if (n == head) head = n->rnext;
        if (n == tail) tail = n->rprev;
        n->rprev = n->rnext = nullptr;
        if(size>0) --size;
    }

    //return the last node in the list
    Node* back() const{
        return tail;
    }
};

//clean list (用cprev/ cnext)
struct CleanList{
    Node* head = nullptr; //MRU clean
    Node* tail = nullptr; //LRU clean
    size_t size = 0;

    void clear(){
        head = tail = nullptr;
        size = 0;
    }

    //push node to front of the list
    void push_front(Node* n){
        n->cprev = nullptr;
        n->cnext = head;
        if (head) head->cprev = n;
        head = n;
        if (!tail) tail = head;
        ++size;
    }

    //remove
    void remove(Node* n){
        if (!n) return;
        if (n->cprev) n->cprev->cnext = n->cnext;
        if (n->cnext) n->cnext->cprev = n->cprev;
        if (n == head) head = n->cnext;
        if (n == tail) tail = n->cprev;
        n->cprev = n->cnext = nullptr;
        if(size>0) --size;
    }

    //return the last node in the list
    Node* back() const{
        return tail;
    }
};

//hash table
struct HashEntry{
    uint64_t key;
    Node* value;
    bool used;
    bool deleted;

    HashEntry()
        : key(0), value(nullptr), used(false), deleted(false) {}
};

class HashTable{
public:
    HashTable(size_t cap = (1u << 22)){
        capacity = cap;
        used_count = 0;
        table.assign(capacity, HashEntry());
    }

    Node* find(uint64_t key) const{
        size_t idx = hash(key);
        size_t start = idx;
        while(table[idx].used){
            //if the entry is deleted and the key is the same, return the value
            if(!table[idx].deleted && table[idx].key == key) 
                return table[idx].value;
            idx = (idx + 1) & (capacity - 1);
            if (idx == start) break; //wrap around
        }
        return nullptr;
    }

    void insert(uint64_t key, Node* value){
        if ((used_count * 2) > capacity){
            rehash(capacity * 2);
        }
        size_t idx = hash(key);
        //find the first empty slot
        while(table[idx].used && !table[idx].deleted){
            if (table[idx].key == key){
                table[idx].value = value;
                return;
            }
            idx = (idx + 1) & (capacity - 1);
        }
        table[idx].key = key;
        table[idx].value = value;
        table[idx].used = true;
        table[idx].deleted = false;
        ++used_count;
    }

    void erase(uint64_t key){
        size_t idx = hash(key);
        size_t start = idx;
        while(table[idx].used){
            if(!table[idx].deleted && table[idx].key == key){
                table[idx].deleted = true;
                table[idx].value = nullptr;
                return;
            }
            idx = (idx + 1) & (capacity - 1);
            if (idx == start) break; //wrap around
        }
    }

    void clear(){
        table.assign(capacity, HashEntry());
        used_count = 0;
    }
private:
    vector<HashEntry> table;
    size_t capacity;
    size_t used_count;

    //hash function
    size_t hash(uint64_t key) const{
        return (key * 11400714819323198485llu) & (capacity - 1);
    }

    void rehash(size_t new_cap){
        if(new_cap & (new_cap - 1)){
            size_t p = 1;
            while(p < new_cap) p <<= 1;
            new_cap = p;
        }
        vector<HashEntry> old = table;
        table.assign(new_cap, HashEntry());
        capacity = new_cap;
        used_count = 0;
        for(auto &e : old){
            if(e.used && !e.deleted && e.value){
                insert(e.key, e.value);
            }
        }
    }
};

//統計用
class CacheSim{
public:
    CacheSim(size_t frames)
        :frame_limit(frames), hits(0), misses(0), writebacks(0), total_refs(0){}
    
    virtual ~CacheSim(){}

    virtual void access(uint64_t page, bool is_write) = 0;

    size_t get_hits() const{ return hits;}
    size_t get_misses() const{ return misses;}
    size_t get_writebacks() const{ return writebacks;}
    size_t get_total_refs() const{ return total_refs;}
protected:
    size_t frame_limit;
    size_t hits;
    size_t misses;
    size_t writebacks;
    size_t total_refs;
};

class LRUcache: public CacheSim{
public:
    LRUcache(size_t frames)
        :CacheSim(frames), table(1 <<22){}
    
    void access(uint64_t page, bool is_write) override{
        ++total_refs;

        Node* n = table.find(page);
        if(n){
            //有找到，hit移到MRU
            ++hits;
            if(is_write){
                n->dirty = true;
            }
            list.move_to_front(n);
        }else{
            //MISS
            ++misses;                
            if(list.size >= frame_limit){
                //full
                evict_one();
            }
            Node* nn = new Node(page, is_write);
            list.push_front(nn);
            table.insert(page, nn);
        }
    }

    ~LRUcache() override {
        //free all nodes
        Node* cur = list.head;
        while(cur){
            Node* next = cur->rnext;
            delete cur;
            cur = next;
        }
    }
private:
    RegionList list;
    HashTable table;

    void evict_one(){
        Node* victim = list.back();
        if (!victim) return;
        if (victim->dirty) ++writebacks;
        table.erase(victim->page);
        list.remove(victim);
        delete victim;
    }
};

class CFLRUcache: public CacheSim{
public:
    CFLRUcache(size_t frames)
        :CacheSim(frames), table(1 <<22){clean_limit = frame_limit / 4; working_limit = frame_limit - clean_limit;}
    
    void access(uint64_t page, bool is_write) override{
        ++total_refs;

        Node* n = table.find(page);
        if(n){
            ++hits;
            //從CLEAN變DIRTY要移出CLEAN_ONLY list
            if (is_write && !n->dirty){
                n->dirty = true;
                if(n->region == REGION_CLEAN){
                    clean_only.remove(n);
                }
            }

            //rereference:統一移到MRU(worling list)
            if(n->region == REGION_WORK){
                working.move_to_front(n);
            }else{ //在CLEAN list要移到WORKING list
                clean_first.remove(n);
                if(!n->dirty){
                    clean_only.remove(n);
                }
                n->region = REGION_WORK;
                working.push_front(n);
            }
        }else{
            //MISS
            ++misses;
            if(working.size + clean_first.size >= frame_limit){ //full
                evict_one();
            }
            //新的page必放入working list
            Node* nn = new Node(page, is_write);
            nn->region = REGION_WORK;
            working.push_front(nn);
            table.insert(page, nn);
        }

        if(working.size > working_limit && clean_first.size < clean_limit){
            migrate_one();
        }
    }
    ~CFLRUcache() override {
        //free all nodes
        Node* cur = working.head;
        while(cur){
            Node* next = cur->rnext;
            delete cur;
            cur = next;
        }
        cur = clean_first.head;
        while(cur){
            Node* next = cur->rnext;
            delete cur;
            cur = next;
        }
    }
private:
    RegionList working;
    RegionList clean_first;
    CleanList clean_only;
    HashTable table;

    size_t clean_limit;
    size_t working_limit;

    void migrate_one(){
        Node* n = working.back();
        if (!n) return;
        working.remove(n); //先刪除舊的
        n->region = REGION_CLEAN;
        clean_first.push_front(n);
        if(!n->dirty){
            clean_only.push_front(n);
        }
    }

    void evict_one(){
        Node* victim = nullptr;

        if (clean_only.size > 0){
            victim = clean_only.back(); //clean only list的LRU
        }else {
            victim = clean_first.back();
        }

        if (!victim) return;
        if (victim->dirty) ++writebacks;
        table.erase(victim->page);

        if(!victim->dirty){
            clean_only.remove(victim);
        }
        clean_first.remove(victim);
        delete victim;
    }
};

bool load_trace(const char* filename, vector<Trace>& traces){
    FILE* fp = std::fopen(filename, "r");
    if(!fp){
        std::perror("fopen");
        return false;
    }
    traces.clear();
    traces.reserve(50000000);
    
    char op;
    unsigned long long addr;
    while(std::fscanf(fp, " %c %llx", &op, &addr) == 2){ //%c前空格是為了忽略空格
        Trace t;
        t.is_write = (op == 'W');
        uint64_t offset = static_cast<uint64_t>(addr);
        t.page = offset / PAGE_SIZE;
        traces.push_back(t);
    }
    std::fclose(fp);
    return true;
}

//計算時間
double elapsed_seconds(const timeval& start, const timeval& end){
    return (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
}

//跑LRU
void run_lru(const vector<Trace>& traces){
    int frames_list[5] = {4096, 8192, 16384, 32768, 65536};

    printf("LRU policy:\n");
    printf("%-8s%-12s%-12s%-20s%-16s\n",
        "Frame", "Hit", "Miss", "Page fault ratio", "Write back count");

    timeval start{}, end{};
    gettimeofday(&start, nullptr);

    for(int f : frames_list){
        LRUcache cache(f);
        
        for(const auto& tr : traces){
            cache.access(tr.page, tr.is_write);
        }
        size_t hits = cache.get_hits();
        size_t miss = cache.get_misses();
        size_t wb = cache.get_writebacks();
        double ratio = (double)miss / (double)cache.get_total_refs();
        printf("%-8d%-12zu%-12zu%-20.10f%-16zu\n",
            f, hits, miss, ratio, wb);
     
    }
    gettimeofday(&end, nullptr);
    double sec = elapsed_seconds(start, end);
    printf("Total elapsed time: %0.6f sec\n", sec);
}

//跑CFLRU
void run_cf(const vector<Trace>& traces){
    int frames_list[5] = {4096, 8192, 16384, 32768, 65536};

    printf("\n");
    printf("CFLRU policy:\n");
    printf("%-8s%-12s%-12s%-20s%-16s\n",
        "Frame", "Hit", "Miss", "Page fault ratio", "Write back count");
 

    timeval start{}, end{};
    gettimeofday(&start, nullptr);

    for(int f : frames_list){
        CFLRUcache cache(f);
        
        for(const auto& tr : traces){
            cache.access(tr.page, tr.is_write);
        }
        size_t hits = cache.get_hits();
        size_t miss = cache.get_misses();
        size_t wb = cache.get_writebacks();
        double ratio = (double)miss / (double)cache.get_total_refs();
        printf("%-8d%-12zu%-12zu%-20.10f%-16zu\n",
            f, hits, miss, ratio, wb);
     
    }
    gettimeofday(&end, nullptr);
    double sec = elapsed_seconds(start, end);
    printf("Total elapsed time: %0.6f sec\n", sec);
}

int main(int argc, char* argv[]){
    if(argc != 2){
        std::fprintf(stderr, "Usage: %s <trace file>\n", argv[0]);
        return 1;
    }

    vector<Trace> traces;
    if(!load_trace(argv[1], traces)){
        return 1;
    }

    run_lru(traces);
    run_cf(traces);
    return 0;
}