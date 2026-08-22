#!/usr/bin/env python3
"""One-shot patcher: make load_mods() bytecode-first (mods/compiled/*.lc),
source .lua opt-in via PIVOTKIT_ALLOW_SOURCE=1."""
import sys

BS = chr(92)
DQ = chr(34)
NL = chr(10)

p = "src/pivotkit.cpp"
lines = open(p, encoding="utf-8").readlines()

# locate the real definition (line with 'static void load_mods(const char* modDir)' + '{')
start = None
for i, l in enumerate(lines):
    if l.startswith("static void load_mods(const char* modDir)") and "{" in lines[i + 1]:
        start = i
        break
assert start is not None, "definition not found"

# find end of function: the line '}' at column 0 after start
end = None
for j in range(start + 2, len(lines)):
    if lines[j] == "}" + NL:
        end = j
        break
assert end is not None

new_body = [
    "static void load_mods(const char* modDir)" + NL,
    "{" + NL,
    "    /* Compiled-mod policy: load Lua BYTECODE only (mods/compiled/*.lc," + NL,
    "     * built by tools/pkcompile.py). Source .lua files are skipped with" + NL,
    "     * a warning unless PIVOTKIT_ALLOW_SOURCE=1 is set. */" + NL,
    "    char compiledDir[MAX_PATH];" + NL,
    '    _snprintf(compiledDir, sizeof(compiledDir), "%s' + BS + BS + 'compiled", modDir);' + NL,
    "    if (GetFileAttributesA(compiledDir) != INVALID_FILE_ATTRIBUTES) {" + NL,
    "        char pattern[MAX_PATH];" + NL,
    '        _snprintf(pattern, sizeof(pattern), "%s' + BS + BS + '*.lc", compiledDir);' + NL,
    "        char names[MAX_MOD_FILES][MAX_PATH];" + NL,
    "        int count = 0;" + NL,
    "        WIN32_FIND_DATAA fd;" + NL,
    "        HANDLE h = FindFirstFileA(pattern, &fd);" + NL,
    "        if (h != INVALID_HANDLE_VALUE) {" + NL,
    "            do {" + NL,
    "                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;" + NL,
    "                if (count >= MAX_MOD_FILES) continue;" + NL,
    "                strncpy(names[count], fd.cFileName, MAX_PATH - 1);" + NL,
    "                names[count][MAX_PATH - 1] = 0;" + NL,
    "                count++;" + NL,
    "            } while (FindNextFileA(h, &fd));" + NL,
    "            FindClose(h);" + NL,
    "        }" + NL,
    "        qsort(names, count, sizeof(names[0]), compare_mod_names);" + NL,
    "        for (int i = 0; i < count; i++) {" + NL,
    "            char path[MAX_PATH];" + NL,
    '            _snprintf(path, sizeof(path), "%s' + BS + BS + '%s", compiledDir, names[i]);' + NL,
    '            if (g_log) { fprintf(g_log, "pivotkit: loading compiled mod %s' + BS + "n" + '", names[i]); fflush(g_log); }' + NL,
    "            if (luaL_dofile(g_L, path) != LUA_OK) {" + NL,
    '                if (g_log) { fprintf(g_log, "pivotkit: mod error: %s' + BS + "n" + '", lua_tostring(g_L, -1)); fflush(g_log); }' + NL,
    "                lua_pop(g_L, 1);" + NL,
    "            }" + NL,
    "        }" + NL,
    "    }" + NL,
    "    char allowSource[8];" + NL,
    "    if (!(GetEnvironmentVariableA(" + DQ + "PIVOTKIT_ALLOW_SOURCE" + DQ + ", allowSource, sizeof(allowSource)) > 0)) {" + NL,
    "        char pattern[MAX_PATH];" + NL,
    '        _snprintf(pattern, sizeof(pattern), "%s' + BS + BS + '*.lua", modDir);' + NL,
    "        WIN32_FIND_DATAA fd;" + NL,
    "        HANDLE h = FindFirstFileA(pattern, &fd);" + NL,
    "        if (h != INVALID_HANDLE_VALUE) {" + NL,
    "            do {" + NL,
    "                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;" + NL,
    "                char lcpath[MAX_PATH];" + NL,
    '                _snprintf(lcpath, sizeof(lcpath), "%s' + BS + BS + 'compiled' + BS + BS + '%.240s", modDir, fd.cFileName);' + NL,
    "                char* dot = strrchr(lcpath, '.');" + NL,
    '                if (dot) strcpy(dot, ".lc");' + NL,
    "                if (GetFileAttributesA(lcpath) == INVALID_FILE_ATTRIBUTES)" + NL,
    '                    if (g_log) { fprintf(g_log, "pivotkit: skipping uncompiled %s (run tools/pkcompile.py)' + BS + "n" + '", fd.cFileName); fflush(g_log); }' + NL,
    "            } while (FindNextFileA(h, &fd));" + NL,
    "            FindClose(h);" + NL,
    "        }" + NL,
    "        return;" + NL,
    "    }" + NL,
    "    char pattern[MAX_PATH];" + NL,
    '    _snprintf(pattern, sizeof(pattern), "%s' + BS + BS + '*.lua", modDir);' + NL,
    "    char names[MAX_MOD_FILES][MAX_PATH];" + NL,
    "    int count = 0;" + NL,
    "    WIN32_FIND_DATAA fd;" + NL,
    "    HANDLE h = FindFirstFileA(pattern, &fd);" + NL,
    "    if (h == INVALID_HANDLE_VALUE) return;" + NL,
    "    do {" + NL,
    "        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;" + NL,
    "        if (count >= MAX_MOD_FILES) continue;" + NL,
    "        strncpy(names[count], fd.cFileName, MAX_PATH - 1);" + NL,
    "        names[count][MAX_PATH - 1] = 0;" + NL,
    "        count++;" + NL,
    "    } while (FindNextFileA(h, &fd));" + NL,
    "    FindClose(h);" + NL,
    "    qsort(names, count, sizeof(names[0]), compare_mod_names);" + NL,
    "    for (int i = 0; i < count; i++) {" + NL,
    "        char path[MAX_PATH];" + NL,
    '        _snprintf(path, sizeof(path), "%s' + BS + BS + '%s", modDir, names[i]);' + NL,
    '        if (g_log) { fprintf(g_log, "pivotkit: loading mod %s' + BS + "n" + '", names[i]); fflush(g_log); }' + NL,
    "        if (luaL_dofile(g_L, path) != LUA_OK) {" + NL,
    '            if (g_log) { fprintf(g_log, "pivotkit: mod error: %s' + BS + "n" + '", lua_tostring(g_L, -1)); fflush(g_log); }' + NL,
    "            lua_pop(g_L, 1);" + NL,
    "        }" + NL,
    "    }" + NL,
    "}" + NL,
]

lines[start:end + 1] = new_body
open(p, "w", encoding="utf-8", newline="").writelines(lines)
print("load_mods replaced: bytecode-first, source opt-in")
