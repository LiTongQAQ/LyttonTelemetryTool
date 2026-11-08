# Qt 自带的 llvm-mingw（clang 17 + UCRT）。Swofty 插件和本仓库的程序都用它编译。
set(CMAKE_SYSTEM_NAME Windows)
set(LLVM_MINGW "D:/YzcWorkSpace/Qt/Tools/llvm-mingw1706_64" CACHE PATH "llvm-mingw root")
set(CMAKE_C_COMPILER "${LLVM_MINGW}/bin/clang.exe")
set(CMAKE_CXX_COMPILER "${LLVM_MINGW}/bin/clang++.exe")
set(CMAKE_RC_COMPILER "${LLVM_MINGW}/bin/llvm-windres.exe")  # GNU-style: emits a COFF object lld can link
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
