#!/usr/bin/env python
"""州渚的 GDExtension 构建。

    scons                      # 调试版（默认 target=template_debug）
    scons target=template_release
    scons -c                   # 清理

产物直接落在 game/bin/，Godot 那边靠 game/bin/zhouzhu.gdextension 找它。
"""

env = SConscript("extern/godot-cpp/SConstruct")

env.Append(CPPPATH=["src/"])
sources = Glob("src/*.cpp")

libname = "zhouzhu"
target = "game/bin/{}{}{}".format(libname, env["suffix"], env["SHLIBSUFFIX"])

library = env.SharedLibrary(target, source=sources)
Default(library)
