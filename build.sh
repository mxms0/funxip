clang unxip.c msqueue/queue_semiblocking.c -o unx  -isysroot \
    /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk \
    -L/usr/local/opt/bzip2/lib -I/usr/local/opt/bzip2/include -lbz2 \
    -I/usr/local/opt/zlib/include -L/usr/local/opt/zlib/lib -lz -g \
    -I/usr/local/opt/libxml2/include -L/usr/local/opt/libxml2/lib -lxml2\
    -I/usr/local/include -llzma \
    -Wno-incompatible-pointer-types
