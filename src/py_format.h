#pragma once

#include <cstdint>
#include <string>

#include "skyisle/json.hpp"

// 与 Python 同样的写法（行星计划 P6e）：进程内生成写出的文件要与 Python 那条路（生成器前端 + tools/export_skyisle.py / export_planet.py）逐字节相同，
// 凡是 Python 用 str() / repr() / f-string / json.dumps 写出来的数，这里照同样的规矩写。不含 Godot 头。
namespace pyfmt {

// repr(float)：最短的、读回逐位相同的十进制（与 CPython 的 float_repr 同：小数点位置 decpt ≤ −4 或 > 16 用指数，指数至少两位；整数值补 ".0"）
std::string repr(double p_x);
// str(int)
std::string str(int64_t p_v);
// format(x, "g")（6 位有效数字、去掉尾零，同 C 的 %g）
std::string fmt_g(double p_x);
// format(x, ".Nf")（精确的十进制舍入）
std::string fmt_f(double p_x, int p_digits);

// json.dumps(v, ensure_ascii=False, indent=p_indent)：键的次序照对象里的次序；浮点按 repr，NaN / ±inf 写 NaN / Infinity / -Infinity（Python 的默认）
// p_crlf：换行写成 \r\n（Windows 上 Path.write_text 不给 newline 时，\n 会被换成 \r\n）
std::string json_dumps(const skyisle::Json &p_value, int p_indent, bool p_crlf = false);

// SHA-1 的十六进制（stamp.json 的配置哈希）
std::string sha1_hex(const std::string &p_data);

} // namespace pyfmt
