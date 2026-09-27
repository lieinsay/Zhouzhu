#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// zlib 流（RFC 1950）/ DEFLATE（RFC 1951）的解压：纯 C++，不引 zlib。只解不压（压在导出脚本里用 Python 的 zlib）。
// 行星计划 P5 的逐日天气包（weather.bin）用它：八千群一年的逐日天气无损压到 9.5 MB，解开 27 MB。
// 查表解码（前 10 位一张表，更长的码逐位走规范霍夫曼），校验 Adler-32。
// p_expected：解开应有多少字节（包头记着；预先留好地方，多了少了都算错）；0 = 不知道。
bool zlib_inflate(const uint8_t *p_data, size_t p_size, std::vector<uint8_t> &r_out, size_t p_expected, std::string &r_error);
// 裸的 DEFLATE 流（没有 zlib 的头尾）
bool raw_inflate(const uint8_t *p_data, size_t p_size, std::vector<uint8_t> &r_out, std::string &r_error, size_t *r_consumed = nullptr);
