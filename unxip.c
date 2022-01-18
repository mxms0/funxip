// Faster unxip, hopefully
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <zlib.h>
#include <bzlib.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include "unxip.h"

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
    FILE *fp = fopen("/Users/max/Downloads/Xcode_13.2.1.xip", "rb");
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

    fseek(fp, content.metadata.location.offset, SEEK_CUR);
    uint8_t *metadata_buf = malloc(content.metadata.location.length);
    uint8_t *metadata_decompressed = malloc(content.metadata.location.size + 1);
    if (!metadata_buf || !metadata_decompressed) BAD();

    ret = fread(metadata_buf, content.metadata.location.length, 1, fp);
    if (ret != 1) BAD();

    unsigned int destLen = content.metadata.location.size;
    ret = BZ2_bzBuffToBuffDecompress(metadata_decompressed, &destLen, 
            metadata_buf, content.metadata.location.length, 0, 4);
    printf("%d\n", ret);
    write(1, metadata_decompressed, content.metadata.location.size);
    write(1, "\n", 1);

    fseek(fp, content.content.offset, SEEK_SET);

    fclose(fp);

    return 0;
}
