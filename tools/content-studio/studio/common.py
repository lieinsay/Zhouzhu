"""严格 JSON、兼容旧草稿的指纹、原子工具文件写入。"""
import hashlib
import json
import math
import os
import re
import uuid
from datetime import datetime, timezone
from decimal import Decimal
from pathlib import Path


class StudioError(Exception):
    def __init__(self, status, message):
        super().__init__(message)
        self.status = status


def finite(value):
    try:
        return type(value) in (int, float) and math.isfinite(value)
    except OverflowError:
        return False


def integer(value):
    return finite(value) and value == math.floor(value)


def loads(text):
    def invalid(value):
        raise ValueError(f"不支持 JSON 数值 {value}")
    def number(value):
        result = float(value)
        if not math.isfinite(result):
            invalid(value)
        return result
    return json.loads(text, parse_constant=invalid, parse_float=number)


def quote(value):
    result = json.dumps(value, ensure_ascii=False)
    return re.sub(r"[\ud800-\udfff]", lambda m: f"\\u{ord(m[0]):04x}", result)


def js_number(value):
    """保留旧 JSON.stringify 指纹所用的十进制表示，不在运行时调用 Node。"""
    if not finite(value):
        raise ValueError("JSON 数值必须有限")
    if value == 0:
        return "0"
    if type(value) is int:
        if abs(value) <= 9007199254740991:
            return str(value)
        value = float(value)  # 旧格式的数值语义是 ECMAScript Number。
    short = repr(value).lower()
    if 1e-6 <= abs(value) < 1e21:
        result = format(Decimal(short), "f")
        return result.rstrip("0").rstrip(".") if "." in result else result
    mantissa, exponent = short.split("e") if "e" in short else (short, "0")
    mantissa = mantissa.removesuffix(".0")
    power = int(exponent)
    return f"{mantissa}e{'+' if power >= 0 else '-'}{abs(power)}"


def js_dumps(value):
    """旧 v1 草稿指纹兼容；序列化顺序与浏览器 JSON.stringify 一致。"""
    if value is None:
        return "null"
    if type(value) is bool:
        return "true" if value else "false"
    if type(value) in (int, float):
        return js_number(value)
    if isinstance(value, str):
        return quote(value)
    if isinstance(value, list):
        return "[" + ",".join(js_dumps(row) for row in value) + "]"
    if isinstance(value, dict):
        numeric = [key for key in value if re.fullmatch(r"0|[1-9]\d*", key) and int(key) < 4294967295]
        keys = sorted(numeric, key=int) + [key for key in value if key not in numeric]
        return "{" + ",".join(quote(key) + ":" + js_dumps(value[key]) for key in keys) + "}"
    raise ValueError("不支持的 JSON 类型")


def digest(value):
    return hashlib.sha256(js_dumps(value).encode("utf-8")).hexdigest()


def timestamp():
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def read_json(path, fallback=None):
    try:
        return loads(Path(path).read_text(encoding="utf-8"))
    except FileNotFoundError:
        return fallback


def atomic_json(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + "." + str(uuid.uuid4()) + ".tmp")
    try:
        with temporary.open("x", encoding="utf-8", newline="\n") as handle:
            handle.write(json.dumps(data, ensure_ascii=False, allow_nan=False, indent=2) + "\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)
