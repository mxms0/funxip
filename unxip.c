// Faster unxip, hopefully
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>

#include <zlib.h>
#include <lzma.h>
#include <bzlib.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include "unxip.h"
#include "msqueue/queue.h"

struct queue_root *work_queue = NULL;

void print_file_info(file_info *info) {
    printf("file_location = (length = 0x%llx, offset = 0x%llx, size = 0x%llx)\n", 
            info->location.length, info->location.offset, info->location.size); 
}

void printNode(xmlNode *node) {
#define PRINT_MEMBER(thing, specifier) do { \
    printf(#thing " = " specifier ", ", thing); \
} while (0);

    printf("{ ");
    PRINT_MEMBER(node->type, "%d");
    PRINT_MEMBER(node->name, "\"%s\"");
    // PRINT_MEMBER(node->parent, "%p");
    // PRINT_MEMBER(node->children, "%p");
    // PRINT_MEMBER(node->last, "%p");
    // PRINT_MEMBER(node->next, "%p");
    // PRINT_MEMBER(node->prev, "%p");
    // PRINT_MEMBER(node->doc, "%p");
    PRINT_MEMBER(node->content, "%p");
    if (node->properties) 
        PRINT_MEMBER(node->properties, "%p");

    printf("}\n");
}

static const char *const SpecialFileNameContent = "Content";
static const char *const SpecialFileNameMetadata = "Metadata";

int parse_file_data_xmlnode(xmlNode *file_node, file_location *location) {
    xmlNode *iter = file_node->children;

    while (iter) {
        char *value = xmlNodeGetContent(iter);
        if (NODE_NAME_EQUALS(iter, "length")) {
            location->length = atoll(value);
        }
        else if (NODE_NAME_EQUALS(iter, "offset")) {
            location->offset = atoll(value);
        }
        else if (NODE_NAME_EQUALS(iter, "size")) {
            location->size = atoll(value);
        }

        iter = iter->next;
    }

    return 0;
}

struct work_context {
    const char *file_path;
    uint64_t thread_id;
    uint64_t *waiting_states;
};

void worker_thread(void *_context) {
    int ret = 0;
    struct work_context *context = (struct work_context *)_context;
    uint8_t *lzData = malloc(0x1000000);
    uint8_t *lmData = malloc(0x1000000);

    int fd = open(context->file_path, O_RDONLY);
    if (!fd) BAD();
    uint64_t thread_id = context->thread_id;
    lzma_stream lzs = LZMA_STREAM_INIT;
    lzma_stream_decoder(&lzs, UINT64_MAX, LZMA_CONCATENATED);
    // Open file to work on
    // Grab offset to start at from a worker thread
    // Write file out to disk
    while ((1)) {
        // We are waiting for work.
        context->waiting_states[thread_id] = 1;

        struct queue_head *work_item = queue_get(work_queue);
        if (!work_item) {
            usleep(99);
            continue;
        }

        uint64_t file_offset = (uint64_t)work_item->content;
        lseek(fd, file_offset, SEEK_SET);
        struct pbzxChunkHeader chunkHeader = { 0 };
        read(fd, &chunkHeader, sizeof(chunkHeader));
        chunkHeader.length = __builtin_bswap64(chunkHeader.length);
        
        read(fd, lzData, chunkHeader.length);
        if (memcmp(lzData, "\xfd""7zXZ", 5)) {
            printf("wat dis: %hhx%c%c%c%c\n", lzData[0], lzData[1], lzData[2], lzData[3], lzData[4]);
        }

        lzs.next_in = (typeof(lzs.next_in)) lzData;
#define min(x, y) ((x) < (y) ? (x) : (y))
        lzs.avail_in = chunkHeader.length;
        while (lzs.avail_in) {
            lzs.next_out = (typeof(lzs.next_out)) lmData;
            lzs.avail_out = 0x1000000;
            if (lzma_code(&lzs, LZMA_RUN) != LZMA_OK) {
                fprintf(stderr, "LZMA failure");
            }
            // cpio_out(zbuf, ZBSZ - zs.avail_out);
        }

        printf("[%d] Finished work\n", thread_id);
        // Grab queue lock to remove work from it
        // Found work, set waiting_state to busy
        context->waiting_states[thread_id] = 0;
        // Begin work
    }
}

int parse_file_xmlnode(xmlNode *file_node, xar_content *info) {
    int ret = 0;

    xmlNode *iter = file_node->children;

    file_info *specific_info = NULL;

    while (iter) {
        if (NODE_NAME_EQUALS(iter, "name")) {
            char *value = xmlNodeGetContent(iter);
            if (strcmp(value, SpecialFileNameContent) == 0) 
                specific_info = &(info->content);
            else if (strcmp(value, SpecialFileNameMetadata) == 0)
                specific_info = &(info->metadata);
            else 
                BAD();
        }

        iter = iter->next;
    }

    // Found the node, let's add the values to the struct
    iter = file_node->children;
    while (iter) {
        if (NODE_NAME_EQUALS(iter, "data")) {
            ret = parse_file_data_xmlnode(iter, &specific_info->location);
        }

        // XXX: Ignore FinderCreateTime and the rest for now, they aren't
        // important to extraction
        iter = iter->next;
    }

    return 0;
}

int process_toc(uint8_t *toc_buffer, uint32_t toc_len, xar_content *content) {
    int ret = 0;

    /*  Document format:
     *  xar { toc { creation-time, checksum, signature, file, file } }
     */
    xmlDocPtr document = xmlReadMemory(toc_buffer, toc_len, "root.xml", NULL, 0);
    xmlNode *rootNode = xmlDocGetRootElement(document);
    // TODO: ASSERT(rootNode->name == 'xar')
    // TODO: ASSERT(rootName->children->name == 'toc')

    for (xmlNode *iter = rootNode->children->children; iter; iter = iter->next) {
        if (NODE_NAME_EQUALS(iter, "file")) {
            // Found file node
            ret = parse_file_xmlnode(iter, content);
            if (ret) return ret;
        }
    }

    xmlFreeDoc(document);
    return ret;
}

int main(int argc, char **argv) {
    const char *file_path = "/Users/max/Downloads/Xcode_13.2.1.xip";
    FILE *fp = fopen(file_path, "rb");
    int ret = 0;

    typedef struct __attribute__((packed)) {
        uint32_t magic;
        uint16_t header_size;
        uint16_t version;
        uint64_t toc_compressed_size;
        uint64_t toc_uncompressed_size;
        uint32_t checksum_algo;
    } xar_header;

    xar_header header = { 0 };
    ret = fread((void *)&header, sizeof(header), 1, fp);
    if (ret != 1) BAD();

    header.magic = htonl(header.magic);
    header.header_size = htons(header.header_size);
    header.version = htons(header.version);
    header.toc_compressed_size = htonll(header.toc_compressed_size);
    header.toc_uncompressed_size = htonll(header.toc_uncompressed_size);
    header.checksum_algo = htonl(header.checksum_algo);

    printf("XAR_HEADER = { \n");
    printf("\t.magic = 0x%x,\n", header.magic);
    printf("\t.header_size = %hd, \n", header.header_size);
    printf("\t.version = %hd, \n", header.version);
    printf("\t.toc_compressed_size = 0x%llx, \n", header.toc_compressed_size);
    printf("\t.toc_uncompressed_size = 0x%llx, \n", header.toc_uncompressed_size);
    printf("\t.checksum_algo = 0x%x\n", header.checksum_algo);
    printf("}\n");

    void *toc_compressed_buf = malloc(header.toc_compressed_size);
    ret = fread(toc_compressed_buf, header.toc_compressed_size, 1, fp);
    if (ret != 1) BAD();

    uint8_t *toc_uncompressed_buf = malloc(header.toc_uncompressed_size + 1);
    if (!toc_uncompressed_buf) BAD();

    uLongf uncompressed_bytes = (uLongf)header.toc_uncompressed_size;
    ret = uncompress(toc_uncompressed_buf, &uncompressed_bytes, toc_compressed_buf, header.toc_compressed_size);
    // printf("ret = %d, out: %d\n", ret, uncompressed_bytes);
    // printf("raw XML doc: %s\n", toc_uncompressed_buf);

    xar_content content = { 0 };
    ret = process_toc(toc_uncompressed_buf, uncompressed_bytes, &content);
    if (ret) BAD();

    printf("Content file info:\n");
    print_file_info(&content.content);
    printf("Metadat file info:\n");
    print_file_info(&content.metadata);

    printf("Seeking to: %llu\n", content.metadata.location.offset);
    printf("Current offset: %llu\n", ftell(fp));

    uint64_t base_offset = (uint64_t)ftell(fp);

    fseek(fp, content.metadata.location.offset, SEEK_CUR);
    uint8_t *metadata_buf = malloc(content.metadata.location.length);
    uint8_t *metadata_decompressed = malloc(content.metadata.location.size + 1);
    if (!metadata_buf || !metadata_decompressed) BAD();

    ret = fread(metadata_buf, content.metadata.location.length, 1, fp);
    if (ret != 1) BAD();

    // This metadata file is basically worthless
    unsigned int destLen = content.metadata.location.size;
    ret = BZ2_bzBuffToBuffDecompress(metadata_decompressed, &destLen, 
            metadata_buf, content.metadata.location.length, 0, 4);
    printf("BZ2 Status: %d\n", ret);
    write(1, metadata_decompressed, content.metadata.location.size);
    write(1, "\n", 1);

    // Ok we got to the meat, let's initialize worker threads
    work_queue = ALLOC_QUEUE_ROOT();

    fseek(fp, content.content.location.offset + base_offset, SEEK_SET);

    uint64_t bytes_consumed = 0;

    struct __attribute__((packed)) {
        char magic[4];
        uint64_t flags;
    } pbzxHeader = { 0 };

    printf("what size is this: %d\n", sizeof(pbzxHeader));

    fread(&pbzxHeader, sizeof(pbzxHeader), 1, fp);
    bytes_consumed += sizeof(pbzxHeader);

    pbzxHeader.flags = __builtin_bswap64(pbzxHeader.flags);

    if (memcmp(&pbzxHeader.magic, "pbzx", 4) != 0) BAD();
    printf("pbzxHeader.flags = 0x%llx\n", pbzxHeader.flags);

    size_t number_of_threads = 4;
    uint64_t waiting_states[number_of_threads];
    memset(&waiting_states, 0, sizeof(waiting_states));

    for (int i = 0; i < number_of_threads; i++) {
        struct work_context *ctx = malloc(sizeof(struct work_context));
        ctx->file_path = file_path;
        ctx->thread_id = i;
        ctx->waiting_states = &waiting_states;

        pthread_t thread = { 0 };
        pthread_create(&thread, NULL, worker_thread, (void *)ctx);
    }

    int flags = pbzxHeader.flags;

    int num_queued = 0;
    while (flags & (1 << 24)) {
        struct pbzxChunkHeader chunkHeader = { 0 };
        uint64_t fpos = ftell(fp);

        fread(&chunkHeader, sizeof(chunkHeader), 1, fp);
        bytes_consumed += sizeof(chunkHeader);
        chunkHeader.length = __builtin_bswap64(chunkHeader.length);

        fseek(fp, chunkHeader.length - 2, SEEK_CUR);
        bytes_consumed += chunkHeader.length - 2;
        uint16_t footer = 0;
        fread(&footer, 2, 1, fp);
        bytes_consumed += 2;

        int is_pod = (chunkHeader.length == 0x1000000);

        if (!is_pod) {
            num_queued++;
            struct queue_head *node = malloc(sizeof(struct queue_head));
            node->content = (void *)fpos;
            queue_put(node, work_queue);
        }

        if (!is_pod && footer != 0x5a59) BAD();
        if (bytes_consumed >= content.content.location.size) break;
    }
    printf("Queued up %d\n", num_queued);

    // Wait for workers to finish up 

    sleep(1000000000);
    printf("\n");
    fclose(fp);

    return 0;
}
