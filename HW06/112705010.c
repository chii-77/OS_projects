#define FUSE_USE_VERSION 30
#include <fuse.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

/*tar前置作業*/
const char *tar_filename = "test.tar"; // 寫死檔案名稱
static FILE *tar_fp = NULL; // tar檔案指標

// tar header結構
struct posix_header{
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
};

// 定義檔案結構，儲存檔案屬性
typedef struct{
    char path[512];
    char name[256];
    mode_t mode;      // 權限
    time_t mtime;     // 修改時間
    off_t size;       // 檔案大小
    off_t offset;     // 檔案內容在tar中的偏移量
    int type;         // 檔案類型(0:檔案, 5:目錄, 2:symbolic link)
    char link_target[512];   // 若是symbolic link，則儲存連結目標
    // 使用者與群組
    uid_t uid;
    gid_t gid;
}TarEntry;

#define MAX_ENTRIES 2000
static TarEntry entries[MAX_ENTRIES];
static int entry_count = 0;

// 八進位轉長整數
long octal_to_long(const char *str, int len){
    char buffer[32];
    strncpy(buffer, str, len);
    buffer[len] = '\0';
    return strtol(buffer, NULL, 8);
}

// 根據路徑尋找檔案路徑
TarEntry *find_entry(const char *path){
    const char *target = path;
    // 去除開頭的/
    if (target[0] == '/'){
        target++;
    }
    // 如果是根目錄，回傳NULL
    if (strlen(target) == 0){
        return NULL;
    }

    //  線性搜索
    for (int i = 0; i < entry_count; i++){
        // 避免/問題
        if (strcmp(entries[i].path, target) == 0){
            return &entries[i];
        }

        // 處理結尾為/的情況
        size_t target_len = strlen(target);
        if (entries[i].type == '5' && strncmp(entries[i].path, target, target_len) == 0 && entries[i].path[target_len] == '/'){
            return &entries[i];
        }
    }
    return NULL;
}

/*初始化tar檔案*/
void parse_tar_file(){
    tar_fp = fopen(tar_filename, "rb");
    if (tar_fp == NULL){
        fprintf(stderr, "Error: failed to open tar file\n");
        exit(1);
    }

    struct posix_header header;
    off_t current_offset = 0;

    //每次讀取前512bytes的header
    while (fread(&header, sizeof(struct posix_header), 1, tar_fp) == 1){
        current_offset += 512;
        
        if (header.name[0] == '\0') continue; //tar檔案結尾為空block

        TarEntry *entry = &entries[entry_count];

        //組合完整路徑
        memset(entry->path, 0, sizeof(entry->path));
        if (header.prefix[0] != '\0'){ //若檔名較長，則補回prefix
            snprintf(entry->path, sizeof(entry->path), "%s/%s", header.prefix, header.name);
        } else {
            strncpy(entry->path, header.name, sizeof(entry->path));
        }

        // 處理結尾為/的情況
        size_t path_len = strlen(entry->path);
        if (path_len > 0 && entry->path[path_len - 1] == '/'){
            entry->path[path_len - 1] = '\0';
        }
        
        // 取得純檔名(for readdir) 先省略
        strncpy(entry->name, header.name, sizeof(entry->name));

        //解析數值欄位
        entry->size = octal_to_long(header.size, sizeof(header.size));
        entry->mtime = octal_to_long(header.mtime, sizeof(header.mtime));
        entry->offset = current_offset;
        entry->type = header.typeflag;
        long mode_val = octal_to_long(header.mode, sizeof(header.mode));
        // 解析使用者與群組
        entry->uid = octal_to_long(header.uid, sizeof(header.uid));
        entry->gid = octal_to_long(header.gid, sizeof(header.gid));

        // 設定權限
        switch(header.typeflag){
            case '0': //regular file
                entry->mode = S_IFREG | mode_val;
                break;
            case '5': //directory
                entry->mode = S_IFDIR | mode_val;
                break;
            case '2': //symbolic link
                entry->mode = S_IFLNK | mode_val;
                strncpy(entry->link_target, header.linkname, sizeof(entry->link_target));
                break;
        }
        entry_count++;

        // 跳過內容，移到下一個block(只有regular file需要跳過)
        if (header.typeflag != '5' && header.typeflag != '2' && entry->size > 0){
            long blocks = (entry->size + 511) / 512; //計算需要跳過的block數
            fseek(tar_fp, blocks * 512, SEEK_CUR);
            current_offset += blocks * 512;
        }
    }
}

/*實作所需功能*/
int my_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
               off_t offset, struct fuse_file_info *fi){
    // 避免跳警報
    (void)offset;
    (void)fi;

    // 加入 . 和 ..
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    // 處理fuse傳入路徑，去除開頭的/
    const char *find_path = path;
    if (find_path[0] == '/'){
        find_path++;
    }
    
    //遍歷檔案
    for (int i = 0; i < entry_count; i++){
        //切割路徑
        char temp_path[512];
        strcpy(temp_path, entries[i].path);

        char *parent_path; // 父路徑
        char *filename; // 檔案名稱

        char *last_slash = strrchr(temp_path, '/'); // 最後一個/的位置切分路徑
        if (last_slash != NULL){
            //檔案在子目錄
            *last_slash = '\0'; //斜線變字串結尾，切斷字串
            parent_path = temp_path; //斜線前為父路徑
            filename = last_slash + 1; //斜線後為檔案名稱
        } else {
            //檔案在根目錄
            parent_path = ""; //父路徑為空字串
            filename = temp_path; //檔案名稱為完整路徑
        }

        // 如果父路徑和傳入路徑相同，則加入檔案名稱
        if (strcmp(parent_path, find_path) == 0){
            filler(buf, filename, NULL, 0);
        }
    }
    return 0;
}

int my_getattr(const char *path, struct stat *stbuf){
    memset(stbuf, 0, sizeof(struct stat)); //初始化struct stat

    if (strcmp(path, "/") == 0){
        stbuf->st_mode = S_IFDIR | 0444; //唯讀
        stbuf->st_nlink = 0; // 直接設為0
        return 0;
    }

    // 查詢檔案
    TarEntry *entry = find_entry(path);
    if (entry == NULL){
        return -ENOENT;
    }

    // 設定檔案屬性
    stbuf->st_mode = entry->mode;
    stbuf->st_nlink = 0; // 直接設為0
    stbuf->st_size = entry->size;
    stbuf->st_mtime = entry->mtime;
    if (entry->uid == 0) {
        // 如果 TAR 裡是 Root，就顯示 Root
        stbuf->st_uid = 0;
        stbuf->st_gid = 0;
    } else {
        // 如果不是 Root，就顯示當前使用者
        stbuf->st_uid = getuid();
        stbuf->st_gid = getgid();
    }
    
    return 0;
}

int my_read(const char *path, char *buf, size_t size, off_t offset,
            struct fuse_file_info *fi){
    (void)fi;

    TarEntry *entry = find_entry(path);
    if (entry == NULL){
        return -ENOENT;
    }

    // 檢查offset是否超出檔案大小
    if (offset >= entry->size) return 0;
    if (offset + size > entry->size) size = entry->size - offset;
    
    // 讀取檔案內容
    fseek(tar_fp, entry->offset + offset, SEEK_SET);
    int result = fread(buf, 1, size, tar_fp);

    return result;
}

int my_readlink(const char *path, char *buf, size_t size){
    TarEntry *entry = find_entry(path);
    if (entry == NULL){
        return -ENOENT;
    }

    // 檢查是否為symbolic link
    if (entry->type != '2'){
        return -EINVAL; //不是symbolic link
    }

    strncpy(buf, entry->link_target, size - 1);
    buf[size - 1] = '\0';

    return 0;
}

static struct fuse_operations my_oper;

int main(int argc, char *argv[]){
    parse_tar_file();

    // 設定fuse操作
    memset(&my_oper, 0, sizeof(my_oper));
    my_oper.readdir = my_readdir;
    my_oper.getattr = my_getattr;
    my_oper.read = my_read;
    my_oper.readlink = my_readlink;

    return fuse_main(argc, argv, &my_oper, NULL);
}
