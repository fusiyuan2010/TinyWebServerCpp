#ifndef _ZLIB_COMPRESSION_HPP_
#define _ZLIB_COMPRESSION_HPP_
#ifdef HTTP_COMPRESSION
#include <string>

namespace tws{

std::string zlib_compress(const std::string& str,
                            int compressionlevel = 5);

std::string zlib_decompress(const std::string& str);

}

#endif
#endif

