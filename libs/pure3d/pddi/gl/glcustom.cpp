// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

//=============================================================================
// Linux port: user material shaders (see glcustom.hpp, shaders/README.md).
//=============================================================================

#include <pddi/gl/glcustom.hpp>
#include <pddi/gl/glpipeline.hpp>

#include <SDL2/SDL.h>
#include <dirent.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

// port/compat/shar_mods.h: the files in the nth mod's root subfolder that has any
extern "C" int shar_ModConfigFolderNth(const char* folder, int nth, char** texts, size_t* sizes, char** names, int max,
                                       const char** modName);

const int MAX_RULES = 256;
const int MAX_FILES = pglMaxCustomPrograms;
const int MAX_MOD_FILES = 64;

enum RuleKind { RULE_MATERIAL, RULE_FILE, RULE_TYPE, RULE_KINDS };      // [materials], [files], [types]: matched in this order

struct Rule
{
    int kind;                   // RuleKind
    char pattern[128];          // fnmatch pattern, case-insensitive
    int files[pglMaxMaterialPasses];    // indices in s_files, one per pass
    int passes;
};

struct ShaderFile
{
    char name[300];             // the user's path, or "<mod>:<file>"
    char path[600];             // on disk (user files), "" for a mod's
    time_t mtime;
    int slot;                   // pipeline program slot, -1 when it does not build
};

static Rule s_rules[MAX_RULES];
static int s_ruleCount;
static ShaderFile s_files[MAX_FILES];
static int s_fileCount;
static char s_folder[512];
static char s_iniPath[600];
static time_t s_iniTime;
static unsigned s_generation = 1;
static Uint32 s_lastCheck;
static bool s_loaded;

static time_t FileTime(const char* path)
{
    struct stat st;
    return stat(path, &st) == 0 ? st.st_mtime : 0;
}

// NUL-terminated; size (if given) gets the bytes read
static char* ReadFile(const char* path, size_t* sizeOut = NULL)
{
    FILE* f = fopen(path, "rb");
    if(!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* text = (char*)malloc(size + 1);
    if(text)
    {
        size = (long)fread(text, 1, size, f);
        text[size] = 0;
        if(sizeOut)
            *sizeOut = size;
    }
    fclose(f);
    return text;
}

static char* Trim(char* s)
{
    while(*s == ' ' || *s == '\t')
        s++;
    char* end = s + strlen(s);
    while(end > s && (end[-1] == ' ' || end[-1] == '\t'))
        *--end = 0;
    return s;
}

int pglCustomTarget::BuildCustomSpirv(const char* name, const unsigned*, size_t, const char*)
{
    fprintf(stderr, "PDDI: material shaders: %s is SPIR-V, for the Vulkan renderer only: skipped\n", name);
    return -1;
}

static bool IsSpirv(const char* name)
{
    size_t length = strlen(name);
    return length > 4 && strcasecmp(name + length - 4, ".spv") == 0;
}

// the file's index in s_files, building it the first time; -1 when there is no room. size: source's bytes;
// pragmas: a SPIR-V file's .frag beside it, or NULL
static int AddFile(pglCustomTarget* pipeline, const char* name, const char* path, const char* source, size_t size,
                   const char* pragmas)
{
    for(int i = 0; i < s_fileCount; i++)
    {
        if(strcmp(s_files[i].name, name) == 0)
            return i;
    }
    if(s_fileCount == MAX_FILES)
    {
        fprintf(stderr, "PDDI: material shaders: more than %d files, %s left out\n", MAX_FILES, name);
        return -1;
    }
    ShaderFile& f = s_files[s_fileCount];
    snprintf(f.name, sizeof(f.name), "%s", name);
    snprintf(f.path, sizeof(f.path), "%s", path ? path : "");
    f.mtime = path ? FileTime(path) : 0;
    f.slot = -1;
    if(source && IsSpirv(name))
        f.slot = pipeline->BuildCustomSpirv(name, (const unsigned*)source, size / 4, pragmas ? pragmas : "");
    else if(source)
        f.slot = pipeline->BuildCustomProgram(name, source);
    else
        fprintf(stderr, "PDDI: material shaders: can't read %s\n", path ? path : name);
    return s_fileCount++;
}

// materials.ini: "[types]", "[files]" and "[materials]" sections of "pattern = file" lines; '#' or ';' starts a
// comment.
// files[] / texts[] are the mod's Shaders/ folder (mod and names NULL for the user's folder, whose files are read
// from disk).
static void ParseRules(pglCustomTarget* pipeline, char* text, const char* source, const char* mod, char** names,
                       char** texts, size_t* sizes, int count)
{
    int kind = RULE_TYPE;
    bool inSection = false;
    for(char* line = strtok(text, "\r\n"); line; line = strtok(NULL, "\r\n"))
    {
        char* c = strpbrk(line, "#;");
        if(c)
            *c = 0;
        line = Trim(line);
        if(!*line)
            continue;
        static const char* sections[RULE_KINDS] = { "[materials]", "[files]", "[types]" };
        bool section = false;
        for(int k = 0; k < RULE_KINDS; k++)
        {
            if(strcasecmp(line, sections[k]) == 0)
            {
                kind = k;
                section = inSection = true;
            }
        }
        if(section)
            continue;
        char* eq = strchr(line, '=');
        if(line[0] == '[' || !inSection || !eq)
        {
            fprintf(stderr, "PDDI: material shaders: %s: can't use \"%s\"\n", source, line);
            continue;
        }
        *eq = 0;
        char* pattern = Trim(line);
        if(s_ruleCount == MAX_RULES)
        {
            fprintf(stderr, "PDDI: material shaders: more than %d rules, %s left out\n", MAX_RULES, pattern);
            continue;
        }

        // one or more files, comma-separated: the material is drawn once with each, in order
        Rule& r = s_rules[s_ruleCount];
        r.kind = kind;
        r.passes = 0;
        snprintf(r.pattern, sizeof(r.pattern), "%s", pattern);
        char* list = eq + 1;
        for(char* file = list; file && r.passes < pglMaxMaterialPasses; )
        {
            char* comma = strchr(file, ',');
            if(comma)
                *comma = 0;
            file = Trim(file);
            int index = -1;
            if(*file && !names)
            {
                char path[800];
                snprintf(path, sizeof(path), "%s/%s", s_folder, file);
                size_t size = 0;
                char* shader = ReadFile(path, &size);
                char* pragmas = NULL;
                if(IsSpirv(file))
                {
                    char sidecar[800];
                    snprintf(sidecar, sizeof(sidecar), "%.*s.frag", (int)strlen(path) - 4, path);
                    pragmas = ReadFile(sidecar);
                }
                index = AddFile(pipeline, file, path, shader, size, pragmas);
                free(shader);
                free(pragmas);
            }
            else if(*file)
            {
                char name[300];
                snprintf(name, sizeof(name), "%s:%s", mod, file);
                const char* shader = NULL;
                const char* pragmas = NULL;
                size_t size = 0;
                char sidecar[300];
                snprintf(sidecar, sizeof(sidecar), "%.*s.frag", (int)strlen(file) - 4, file);
                for(int i = 0; i < count; i++)
                {
                    if(strcasecmp(names[i], file) == 0)
                    {
                        shader = texts[i];
                        size = sizes[i];
                    }
                    if(IsSpirv(file) && strcasecmp(names[i], sidecar) == 0)
                        pragmas = texts[i];
                }
                index = AddFile(pipeline, name, NULL, shader, size, pragmas);
            }
            if(index >= 0)
                r.files[r.passes++] = index;
            file = comma ? comma + 1 : NULL;
        }
        if(r.passes)
            s_ruleCount++;
    }
}

static void Load(pglCustomTarget* pipeline)
{
    pipeline->ReleaseCustomPrograms();
    s_ruleCount = 0;
    s_fileCount = 0;
    s_generation++;

    // the user's folder first: its rules win
    s_iniTime = FileTime(s_iniPath);
    char* text = ReadFile(s_iniPath);
    if(text)
    {
        ParseRules(pipeline, text, s_iniPath, NULL, NULL, NULL, NULL, 0);
        free(text);
    }

    // then each mod's Shaders/ folder, in load order: an earlier mod's rules win
    for(int nth = 0; ; nth++)
    {
        char* texts[MAX_MOD_FILES];
        char* names[MAX_MOD_FILES];
        size_t sizes[MAX_MOD_FILES];
        const char* mod = NULL;
        int count = shar_ModConfigFolderNth("Shaders", nth, texts, sizes, names, MAX_MOD_FILES, &mod);
        if(count == 0)
            break;
        char source[300];
        snprintf(source, sizeof(source), "%s Shaders/materials.ini", mod);
        for(int i = 0; i < count; i++)
        {
            if(strcasecmp(names[i], "materials.ini") == 0)
                ParseRules(pipeline, texts[i], source, mod, names, texts, sizes, count);
        }
        for(int i = 0; i < count; i++)
        {
            free(texts[i]);
            free(names[i]);
        }
    }

    if(s_ruleCount)
    {
        fprintf(stderr, "PDDI: material shaders:");
        for(int i = 0; i < s_ruleCount; i++)
        {
            static const char* prefixes[RULE_KINDS] = { "material ", "file ", "" };
            fprintf(stderr, "%s %s%s ->", i ? "," : "", prefixes[s_rules[i].kind], s_rules[i].pattern);
            for(int f = 0; f < s_rules[i].passes; f++)
            {
                const ShaderFile& file = s_files[s_rules[i].files[f]];
                fprintf(stderr, "%s %s%s", f ? " +" : "", file.name, file.slot < 0 ? " (failed)" : "");
            }
        }
        fprintf(stderr, "\n");
    }
}

static bool IsFolder(const char* path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static char s_pack[64];     // the chosen pack (simpsons.ini shaders=, Options > Graphics); "": the default

static const char* ShadersBase()
{
    const char* e = getenv("SHAR_SHADERS");
    return e && *e ? e : "shaders";
}

// a pack, or the base folder itself as one: has a post.ini or materials.ini
static bool HasShaderConfig(const char* folder)
{
    char path[800];
    snprintf(path, sizeof(path), "%s/post.ini", folder);
    if(FileTime(path) != 0)
        return true;
    snprintf(path, sizeof(path), "%s/materials.ini", folder);
    return FileTime(path) != 0;
}

// simpsons.ini's shaders= before the first load: the config manager reads it after the first frames (it then sets the
// same), and a load of the default pack first would compile shaders for nothing
static void PreReadShaderPack()
{
    FILE* f = fopen("simpsons.ini", "r");
    if(!f)
        return;
    char line[256];
    while(fgets(line, sizeof(line), f))
    {
        if(strncasecmp(line, "shaders=", 8) != 0)
            continue;
        char* value = line + 8;
        value[strcspn(value, "\r\n")] = 0;
        if(strcasecmp(value, "yes") != 0 && strcasecmp(value, "on") != 0 && strcasecmp(value, "no") != 0 &&
           strcasecmp(value, "off") != 0 && strcmp(value, "0") != 0 && strcmp(value, "1") != 0)
            snprintf(s_pack, sizeof(s_pack), "%s", value);
        break;
    }
    fclose(f);
}

void pglShaderFolder(char* folder, size_t size)
{
    static bool s_preRead = false;
    if(!s_preRead)
    {
        s_preRead = true;
        PreReadShaderPack();
    }
    const char* base = ShadersBase();
    char path[800];
    if(s_pack[0])
    {
        snprintf(path, sizeof(path), "%s/%s", base, s_pack);
        if(IsFolder(path))
        {
            snprintf(folder, size, "%s", path);
            return;
        }
        static char s_missing[64];          // said once per name: this runs every second
        if(strcmp(s_missing, s_pack) != 0)
        {
            fprintf(stderr, "PDDI: shader pack %s: no such folder\n", path);
            snprintf(s_missing, sizeof(s_missing), "%s", s_pack);
        }
    }
    // the default: a base folder that is a pack itself, else soft-pack, else vanilla-pack
    snprintf(folder, size, "%s", base);
    static const char* const defaults[] = { "soft-pack", "vanilla-pack" };
    for(int i = 0; i < 2 && !HasShaderConfig(base); i++)
    {
        snprintf(path, sizeof(path), "%s/%s", base, defaults[i]);
        if(IsFolder(path))
        {
            snprintf(folder, size, "%s", path);
            return;
        }
    }
}

void pglSetShaderPack(const char* name)
{
    snprintf(s_pack, sizeof(s_pack), "%s", name);
}

const char* pglGetShaderPack()
{
    return s_pack;
}

void pglCurrentShaderPack(char* name, size_t size)
{
    char folder[512];
    pglShaderFolder(folder, sizeof(folder));
    size_t base = strlen(ShadersBase());
    snprintf(name, size, "%s", strlen(folder) > base + 1 ? folder + base + 1 : "");
}

int pglListShaderPacks(char (*names)[64], int max)
{
    const char* base = ShadersBase();
    int count = 0;
    if(HasShaderConfig(base) && count < max)
        names[count++][0] = 0;
    int first = count;
    DIR* d = opendir(base);
    if(!d)
        return count;
    while(struct dirent* e = readdir(d))
    {
        char path[800];
        snprintf(path, sizeof(path), "%s/%s", base, e->d_name);
        if(count >= max || e->d_name[0] == '.' || strlen(e->d_name) >= 64 || !IsFolder(path) || !HasShaderConfig(path))
            continue;
        int k = count++;    // by name: readdir's order is the file system's
        while(k > first && strcmp(names[k - 1], e->d_name) > 0)
        {
            memcpy(names[k], names[k - 1], 64);
            k--;
        }
        snprintf(names[k], 64, "%s", e->d_name);
    }
    closedir(d);
    return count;
}

void pglCustomUpdate(pglCustomTarget* pipeline)
{
    char folder[512];
    if(!s_loaded)
    {
        s_loaded = true;
        pglShaderFolder(s_folder, sizeof(s_folder));
        fprintf(stderr, "PDDI: shaders from %s\n", s_folder);
        snprintf(s_iniPath, sizeof(s_iniPath), "%s/materials.ini", s_folder);
        s_lastCheck = SDL_GetTicks();
        Load(pipeline);
        return;
    }

    // once a second: the user's materials.ini or one of their shader files changed -> reload everything
    Uint32 now = SDL_GetTicks();
    if(now - s_lastCheck < 1000)
        return;
    s_lastCheck = now;
    pglShaderFolder(folder, sizeof(folder));
    bool changed = strcmp(folder, s_folder) != 0;
    if(changed)
    {
        snprintf(s_folder, sizeof(s_folder), "%s", folder);
        snprintf(s_iniPath, sizeof(s_iniPath), "%s/materials.ini", s_folder);
        fprintf(stderr, "PDDI: shaders from %s\n", s_folder);
    }
    changed = changed || FileTime(s_iniPath) != s_iniTime;
    for(int i = 0; i < s_fileCount && !changed; i++)
        changed = s_files[i].path[0] && FileTime(s_files[i].path) != s_files[i].mtime;
    if(changed)
    {
        fprintf(stderr, "PDDI: material shaders: reloading\n");
        Load(pipeline);
    }
}

unsigned pglCustomGeneration()
{
    return s_generation;
}

bool g_pglUserShaders = true;

void pglSetUserShaders(bool on)
{
    if(on == g_pglUserShaders)
        return;
    g_pglUserShaders = on;
    s_generation++;         // materials look their shaders up again (none while off)
    fprintf(stderr, "PDDI: your shaders %s (M toggles)\n", g_pglUserShaders ? "on" : "off");
}

bool pglGetUserShaders()
{
    return g_pglUserShaders;
}

void pglToggleUserShaders()
{
    pglSetUserShaders(!g_pglUserShaders);
}

int pglCustomShadersFor(const char* type, const char* material, const char* file, int* slots)
{
    if(!g_pglUserShaders)
    {
        slots[0] = -1;
        return 1;
    }
    // material rules, then file rules, then type rules; the first match of each kind wins
    const char* subjects[RULE_KINDS] = { material, file, type };
    for(int kind = 0; kind < RULE_KINDS; kind++)
    {
        const char* subject = subjects[kind];
        if(!subject || !*subject)
            continue;
        for(int i = 0; i < s_ruleCount; i++)
        {
            if(s_rules[i].kind != kind || fnmatch(s_rules[i].pattern, subject, FNM_CASEFOLD) != 0)
                continue;
            const Rule& r = s_rules[i];
            int count = 0;
            for(int p = 0; p < r.passes; p++)
            {
                int slot = s_files[r.files[p]].slot;
                if(slot >= 0 || p == 0)
                    slots[count++] = slot;
            }
            return count;
        }
    }
    slots[0] = -1;
    return 1;
}
