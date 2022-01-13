#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <zlib.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#define BAD() do { printf("Error @ %s:%d (%d)\n", __FILE__, __LINE__, ret); exit(0); } while (0);

void process_toc(uint8_t *toc_buffer, uint32_t toc_len) {
    xmlDocPtr doc = xmlReadMemory(toc_buffer, toc_len, "root.xml", NULL, 0);
    printf("doc = %p\n", doc);

    xmlNode *rootNode = xmlDocGetRootElement(doc);
    // ASSERT(rootNode->name == 'xar')
    // ASSERT(rootName->children->name == 'toc')

    for (xmlNode *iter = rootNode->children->children; iter; iter = iter->next) {
        printf("Node name: %s\n", iter->name);
        if (strcmp(iter->name, "file") == 0) {
            // Found file node
            /* <file id="1">
                <data>
                    <length>10747150068</length>
                    <encoding style="application/octet-stream"></encoding>
                    <offset>276</offset>
                    <size>10747150068</size>
                    <extracted-checksum style="sha1">dbd95e2cfe26a1d5dbeb1582acfea371bce5ac91</extracted-checksum>
                    <archived-checksum style="sha1">dbd95e2cfe26a1d5dbeb1582acfea371bce5ac91</archived-checksum>
                </data>
                <FinderCreateTime>
                    <nanoseconds>140733193388032</nanoseconds>
                    <time>1970-01-01T00:00:00</time>
                    </FinderCreateTime>
                    <ctime>2021-12-15T07:22:18Z</ctime>
                    <mtime>2021-12-15T07:22:18Z</mtime>
                    <atime>2021-12-15T06:13:45Z</atime>
                    <group>wheel</group>
                    <gid>0</gid>
                    <user>root</user>
                    <uid>0</uid>
                    <mode>0644</mode>
                    <deviceno>16777220</deviceno>
                    <inode>1695348</inode>
                    <type>file</type>
                    <name>Content</name>
                </file> */
        }
    }

    xmlFreeDoc(doc);
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
    printf("\t.magic = %p,\n", header.magic);
    printf("\t.header_size = %hd, \n", header.header_size);
    printf("\t.version = %hd, \n", header.version);
    printf("\t.toc_compressed_size = %p, \n", header.toc_compressed_size);
    printf("\t.toc_uncompressed_size = %p, \n", header.toc_uncompressed_size);
    printf("\t.checksum_algo = 0x%x\n", header.checksum_algo);
    printf("}\n");

    void *toc_compressed_buf = malloc(header.toc_compressed_size);
    ret = fread(toc_compressed_buf, header.toc_compressed_size, 1, fp);
    if (ret != 1) BAD();

    uint8_t *toc_uncompressed_buf = malloc(header.toc_uncompressed_size + 1);
    if (!toc_uncompressed_buf) BAD();

    uint32_t uncompressed_bytes = header.toc_uncompressed_size;
    ret = uncompress(toc_uncompressed_buf, &uncompressed_bytes, toc_compressed_buf, header.toc_compressed_size);
    printf("ret = %d, out: %d\n", ret, uncompressed_bytes);
    printf("raw XML doc: %s\n", toc_uncompressed_buf);

    process_toc(toc_uncompressed_buf, uncompressed_bytes);

    fclose(fp);

    return 0;
}
