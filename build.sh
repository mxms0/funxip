clang unxip.c -o unx  -isysroot \
    /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk \
    -I/usr/local/opt/zlib/include -L/usr/local/opt/zlib/lib -lz -g \
    -I/usr/local/opt/libxml2/include -L/usr/local/opt/libxml2/lib -lxml2\
    -fsanitize=address
