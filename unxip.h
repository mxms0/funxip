
#define BAD() do { printf("Error @ %s:%d (%d)\n", __FILE__, __LINE__, ret); exit(0); } while (0);
#define NODE_NAME_EQUALS(x, y) (strcmp((char *)x->name, (char *)y) == 0)

typedef enum {
    compression_method_none,
    compression_method_bzip2,
    compression_method_libz,
    compression_method_gzip,
} compression_method;

// Corresponds to `data`
typedef struct {
    size_t length; // How does this differ from size?
    size_t offset;
    size_t size;
    void *extracted_checksum;
    void *archive_checksum;
} file_location;

// Corresponds to `FinderCreateTime`
typedef struct {
    time_t f_ctime;
    time_t f_mtime;
    time_t f_atime;
    size_t f_uid; // uid_t?
    size_t f_mode; // mode_t?
    size_t f_deviceno; 
    size_t f_inode; // inode_t?
    size_t f_type;
    void *f_file_group;
    void *f_name;
    void *f_user;
} file_meta; 

typedef struct {
    size_t fileid;
    compression_method compression;
    file_location location;
    file_meta meta;
} file_info;

typedef struct {
    file_info content;
    file_info metadata;
} xar_content;
