// In Python/instrument.h
#ifndef Py_INSTRUMENT_H
#define Py_INSTRUMENT_H

#include "Python.h"
#include "pycore_frame.h"
#include "pycore_opcode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>

static pid_t pid;
static int flag_config_loaded = 0;
static int core_main_file_flag = 0;
static int current_lineno = 0;

static char name_main_file[1024];
static char main_folder[1024];
static char outlog[1024];
static char forklog_path[1024];
static char python_folder[1024];
static char pip_folder[1024];
static char fork_record_folder[1024];
static int flag_pyinstaller = 0;
static int loop_limit = 50; // 默认值
static int flag_call_uninvoked_function = 0;
static int flag_func_memory = 0;
static char memory_folder[1024];
static int flag_cut_branch = 1; // 默认值
static char object_dump_folder[1024];

static int last_line_main = 0;
static int executed_lines[30000] = {0}; // 假设最大行号不超过30000
static int max_lineno = 0;

struct fork_record
{
    char filename[1024];
    int offset;
    int opcode;
    int oparg;
    int cond;
    /* data */
};
static struct fork_record G_FORK_RECORDS[1000];
static int G_FORK_RECORD_COUNT = 0;



static inline int is_target(const char *filename) {
    if (python_folder[0] != '\0' && strstr(filename, python_folder)) {
        return 0;// 在Python标准库目录中，忽略
    }
    if (pip_folder[0] != '\0' && strstr(filename, pip_folder)) {
        return 0;// 在pip安装目录中，忽略
    }
    if (strstr(filename, "<frozen importlib._bootstrap")) {
        return 0;// 忽略冻结的导入模块
    }
    if (name_main_file[0] != '\0' && strstr(filename, name_main_file)) {
        return 1;// 是目标文件
    }
    return 0;// 其他所有情况都忽略
}


static int load_configuration_file(const char *path) {
    pid = getpid();
    FILE *config_file = fopen(path, "r");
    if (config_file == NULL) {
        printf("[debug log] we cannot open config file in %s\n", path);
        return -1;
    }

    char *line = NULL;
    size_t len = 0;
    ssize_t read;
    int current_config_line = 0;

    while ((read = getline(&line, &len, config_file)) != -1) {
        // 移除行尾的换行符
        if (read > 0 && line[read - 1] == '\n') {
            line[read - 1] = '\0';
            read--; // 更新读取的长度
        }
        if (read == 0) continue; // 跳过空行

        // 找到第一个空格作为分隔符
        char *value = strchr(line, ' ');
        if (value == NULL) continue; // 跳过格式不正确的行
        
        // 跳过所有前导空格
        while (*value == ' ') {
            value++;
        }

        // 按照行号 (current_config_line) 来解析对应的配置项
        switch (current_config_line) {
            case 0: // name_main_file
                strcpy(name_main_file, value);
                printf("name_main_file: %s\n", name_main_file);
                break;
            case 1: // main_folder
                strcpy(main_folder, value);
                printf("main_folder: %s\n", main_folder);
                break;
            case 2: // outlog
                strcpy(outlog, value);
                printf("outlog: %s\n", outlog);
                break;
            case 3: // forklog_path
                strcpy(forklog_path, value);
                printf("forklog_path: %s\n", forklog_path);
                break;
            case 4: // python_folder
                strcpy(python_folder, value);
                printf("python_folder: %s\n", python_folder);
                break;
            case 5: // pip_folder
                strcpy(pip_folder, value);
                printf("pip_folder: %s\n", pip_folder);
                break;
            case 6: // fork_record_folder
                strcpy(fork_record_folder, value);
                printf("fork_record_folder: %s\n", fork_record_folder);
                break;
            case 7: // flag_pyinstaller
                flag_pyinstaller = atoi(value);
                printf("flag_pyinstaller: %d\n", flag_pyinstaller);
                break;
            case 8: // loop_limit
                loop_limit = atoi(value);
                printf("loop_limit: %d\n", loop_limit);
                break;
            case 9: // flag_call_uninvoked_function
                flag_call_uninvoked_function = atoi(value);
                printf("flag_call_uninvoked_function: %d\n", flag_call_uninvoked_function);
                break;
            case 10: // flag_func_memory
                flag_func_memory = atoi(value);
                printf("flag_func_memory: %d\n", flag_func_memory);
                break;
            case 11: // memory_folder
                strcpy(memory_folder, value);
                printf("memory_folder: %s\n", memory_folder);
                break;
            case 12: // flag_cut_branch
                flag_cut_branch = atoi(value);
                printf("flag_cut_branch: %d\n", flag_cut_branch);
                break;
            case 13: // object_dump_folder
                strcpy(object_dump_folder, value);
                printf("object_dump_folder: %s\n", object_dump_folder);
                break;
            default:
                
                break;
        }
        current_config_line++;
    }

    free(line);
    fclose(config_file);
    return 1;
}
// 辅助函数：将一个C字符串路径添加到sys.path列表的末尾
static void append_to_sys_path(const char* path) {
    PyObject *sys_path = PySys_GetObject("path");
    if (sys_path == NULL || !PyList_Check(sys_path)) {
        fprintf(stderr, "[PyForce Error] Cannot get sys.path or it's not a list.\n");
        return;
    }
    PyObject *path_obj = PyUnicode_FromString(path);
    if (path_obj == NULL) {
        fprintf(stderr, "[PyForce Error] Cannot create Python string from path.\n");
        return;
    }
    if (PyList_Append(sys_path, path_obj) != 0) {
        fprintf(stderr, "[PyForce Error] Cannot append to sys.path.\n");
        PyErr_Clear(); // 清除Append可能产生的错误
    }
    Py_DECREF(path_obj);
}
static void add_pip(void) {
    printf("[%d debug log] Adding pip dependencies to sys.path\n", pid);

    // 1. 从配置中读取的python_folder/lib/site-packages
    if (python_folder[0] != '\0') {
        char site_packages_path[2048];
        // 假设python_folder指向Python安装根目录，例如 /usr/
        // 那么site-packages通常在 lib/python3.12/site-packages
        sprintf(site_packages_path, "%s/lib/python3.12/site-packages", python_folder);
        append_to_sys_path(site_packages_path);
        printf("[%d debug log] Appended: %s\n", pid, site_packages_path);
    }

    // 2. 从配置中读取的pip_folder
    if (pip_folder[0] != '\0') {
        append_to_sys_path(pip_folder);
        printf("[%d debug log] Appended: %s\n", pid, pip_folder);
    }

    // 3. 当前工作目录，通常已经在sys.path中，但可以确保一下
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        append_to_sys_path(cwd);
        printf("[%d debug log] Appended current working directory: %s\n", pid, cwd);
    }
}
static void add_pyinstaller(void) {
    printf("[%d debug log] PyInstaller package detected, adding paths.\n", pid);
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("getcwd() error");
        return;
    }

    char path_buffer[2048];

    // 添加PyInstaller解压后的核心路径
    sprintf(path_buffer, "%s/base_library.zip", cwd);
    append_to_sys_path(path_buffer);
    printf("[%d debug log] Appended: %s\n", pid, path_buffer);

    sprintf(path_buffer, "%s/PYZ-00.pyz_extracted", cwd);
    append_to_sys_path(path_buffer);
    printf("[%d debug log] Appended: %s\n", pid, path_buffer);

    // 将当前目录也加入，以防万一
    append_to_sys_path(cwd);
}
// 这个函数只在第一次进入目标主文件时被调用一次
static void initialize_main_analysis_env(void) {
    // 在这里调用路径修改函数，此时sys.path肯定已经准备好了
    if (flag_pyinstaller) {
        add_pyinstaller();
    } else {
        add_pip();
    }

}
static void load_configuration(void) {
    pid = getpid(); // 使用Linux的getpid()
    printf(">>>>>> %5d start()\n", pid);
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        char config_full_path[2048];
        sprintf(config_full_path, "%s/forceConfig.txt", cwd);
        printf("----------------------------------\n");
        printf("We are using configuration file from %s\n", config_full_path);
        int tmp_flag = load_configuration_file(config_full_path);

        if (tmp_flag) {
            printf("configuration successfully loaded\n");
        } else {
            printf("configuration loading failure\n");
        }
        printf("----------------------------------\n");
    } else {
        perror("getcwd() error");
    }
    flag_config_loaded = 1;
}


// --- Function to read the fork record file passed from a parent process ---
static void read_config_from_parent(const char* path) {
    FILE* fp = fopen(path, "r");
    if (fp == NULL) {
        // This is not an error, it just means there's no config to read
        return;
    }

    // Clear any existing records before loading
    G_FORK_RECORD_COUNT = 0;

    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    while ((read = getline(&line, &len, fp)) != -1) {
        if (G_FORK_RECORD_COUNT >= 999) break; // Prevent overflow

        struct fork_record* rec = &G_FORK_RECORDS[G_FORK_RECORD_COUNT];
        int items = sscanf(line, "%s %d %d %d %d",
                           rec->filename, &rec->offset,
                           &rec->opcode, &rec->oparg, &rec->cond);

        if (items == 5) {
            G_FORK_RECORD_COUNT++;
        }
    }

    free(line);
    fclose(fp);
}
// If the record does not exist, add it and return -1 (means we need to fork).
// Else, return the condition for the corresponding record.
static inline int check_or_write_record(const char *filename, int offset, int opcode, int oparg, int cond) {
    for (int i = 0; i < G_FORK_RECORD_COUNT; ++i) {
        if (G_FORK_RECORDS[i].offset == offset &&
            G_FORK_RECORDS[i].opcode == opcode &&
            strcmp(G_FORK_RECORDS[i].filename, filename) == 0)
        {
            // Found it. We are replaying a path. Return the forced condition.
            return G_FORK_RECORDS[i].cond;
        }
    }
    // No record found. This is a new decision point.
    // Add it to our in-memory list.
    if (G_FORK_RECORD_COUNT < 999) {
        struct fork_record* rec = &G_FORK_RECORDS[G_FORK_RECORD_COUNT];
        strncpy(rec->filename, filename, sizeof(rec->filename) - 1);
        rec->offset = offset;
        rec->opcode = opcode;
        rec->oparg = oparg;
        rec->cond = cond;
        G_FORK_RECORD_COUNT++;
        return -1; // Signal that a fork is needed
    }

    // Records are full, stop forking.
    return cond;
}
// This function encapsulates the logic for forking and executing a child process.
static inline int fork_and_exec_child(int child_chosen_cond, int child_jump_target_oparg) {
    // 1. Set the condition for the child's path in the last record
    if (G_FORK_RECORD_COUNT > 0) {
        G_FORK_RECORDS[G_FORK_RECORD_COUNT - 1].cond = child_chosen_cond;
    } else {
        return -1; // Should not happen
    }

    // 2. Prepare the configuration file for the child
    char child_config_path[1024];
    // Use a unique filename in /tmp to avoid race conditions
    sprintf(child_config_path, "/tmp/pyforce_config_%d_%d", getpid(), G_FORK_RECORD_COUNT);

    FILE* fp = fopen(child_config_path, "w");
    if (fp == NULL) {
        perror("Failed to open child config file");
        return -1;
    }
    for (int i = 0; i < G_FORK_RECORD_COUNT; ++i) {
        fprintf(fp, "%s %d %d %d %d\n",
                G_FORK_RECORDS[i].filename, G_FORK_RECORDS[i].offset,
                G_FORK_RECORDS[i].opcode, G_FORK_RECORDS[i].oparg,
                G_FORK_RECORDS[i].cond);
    }
    fclose(fp);

    // 3. Fork the process
    pid_t child_pid = fork();

    if (child_pid == -1) {
        // Fork failed
        perror("fork failed");
        remove(child_config_path); // Clean up config file
        return -1;
    }

    if (child_pid == 0) {
        // --- Child Process ---
        int    argc;
        char** argv;
        wchar_t** wargv;

        // 使用新的API获取 argc 和 argv
        Py_GetArgcArgv(&argc, &wargv);
        // Py_GetArgcArgv 返回 wchar_t**，我们需要将其转换为 char**
        argv = (char**)PyMem_Malloc(sizeof(char*) * (argc + 1));
        if (argv == NULL) _exit(127);
        for(int i = 0; i < argc; i++) {
            argv[i] = Py_EncodeLocale(wargv[i], NULL);
            if (argv[i] == NULL) _exit(127);
        }
        argv[argc] = NULL;

        // Create a new argv array for the child
        char** new_argv = malloc(sizeof(char*) * (argc + 3));
        if (new_argv == NULL) _exit(127);

        for (int i = 0; i < argc; i++) {
            new_argv[i] = argv[i];
        }
        // Add our special arguments
        new_argv[argc] = "--pyforce-config";
        new_argv[argc + 1] = child_config_path;
        new_argv[argc + 2] = NULL;

        // 使用新的API获取可执行文件路径
        wchar_t* w_executable_path = Py_GetProgramFullPath();
        char* executable_path = Py_EncodeLocale(w_executable_path, NULL);
        if (executable_path == NULL) _exit(127);

        // Execute the new process
        execv(executable_path, new_argv);

        perror("execv failed");
        // 清理内存
        free(new_argv);
        for(int i = 0; i < argc; i++) PyMem_RawFree(argv[i]);
        PyMem_Free(argv);
        PyMem_RawFree(executable_path);
        _exit(127);
    }
    else {
        // --- Parent Process ---
        printf("[%d debug log] Forked a new process: %d\n", pid, child_pid);

        // Wait for the child process to complete
        waitpid(child_pid, NULL, 0);
        printf("[%d debug log] Child process %d finished.\n", pid, child_pid);
        
        // After child is done, we don't need its config file anymore
        remove(child_config_path);
        return 1;
    }
}
// --- Function to read config from argv (to be called during initialization) ---
static void read_fork_config_from_argv(void) {
    int    argc;
    wchar_t** wargv;
    char** argv;

    // 使用新的API获取 argc 和 argv
    Py_GetArgcArgv(&argc, &wargv);
    argv = (char**)PyMem_Malloc(sizeof(char*) * (argc + 1));
    if (argv == NULL) return;
    for(int i = 0; i < argc; i++) {
        argv[i] = Py_EncodeLocale(wargv[i], NULL);
        if (argv[i] == NULL) {
            // 清理已分配的内存
            for (int j = 0; j < i; j++) PyMem_RawFree(argv[j]);
            PyMem_Free(argv);
            return;
        }
    }
    argv[argc] = NULL;

    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--pyforce-config") == 0) {
            const char* config_path = argv[i+1];
            printf("Child process detected, loading config from %s\n", config_path);
            read_config_from_parent(config_path);
            break;
        }
    }

}











static inline void PyForce_Log(_PyInterpreterFrame *frame, int opcode) {
    static int main_env_initialized = 0;
    // 1. 单次初始化
    if (!flag_config_loaded) {
        load_configuration();
    }

    // 2. 上下文更新
    const char *current_file_str = "<?>";
    PyObject *filename_obj = frame->f_code->co_filename;
    if (filename_obj != NULL && PyUnicode_Check(filename_obj)) {
        const char* temp_str = PyUnicode_AsUTF8(filename_obj);
        if (temp_str != NULL) {
            current_file_str = temp_str;
        }
    }
    // 更新全局的 current_lineno
    current_lineno = PyUnstable_InterpreterFrame_GetLine(frame);

    // 3. 目标判断与日志记录
    if (is_target(current_file_str)) {
        core_main_file_flag = 1;
        last_line_main = current_lineno;
        if (current_lineno >= 0 && current_lineno < 30000) {
            executed_lines[current_lineno] = 1;
        }
        if (current_lineno > max_lineno) {
            max_lineno = current_lineno;
        }
        // 打印执行日志
        const char *opname = _PyOpcode_OpName[opcode];
        if (opname == NULL) opname = "<UNKNOWN>";
        const char *current_frame_name = "<?>";
        PyObject *frame_name_obj = frame->f_code->co_name;
        if(frame_name_obj != NULL && PyUnicode_Check(frame_name_obj)) {
            const char* temp_str = PyUnicode_AsUTF8(frame_name_obj);
            if (temp_str != NULL) {
                current_frame_name = temp_str;
            }
        }
        const char* basename = strrchr(current_file_str, '/');
        if (basename != NULL) {
            basename++;
        } else {
            basename = current_file_str;
        }
        printf("[%5d execution log] Execute %s:%-3d in %-20s | %s\n",
               pid, basename, current_lineno, current_frame_name, opname);
        fflush(stdout);

    } else {
        core_main_file_flag = 0;
    }

    // 4. 主分析环境单次初始化,只在第一次进入目标文件时执行一次
    if (core_main_file_flag && !main_env_initialized) {
       
        // a. 初始化sys.path
        initialize_main_analysis_env();
        
        // b. 初始化forking环境
        // 如果是子进程，加载决策链;如果是主进程，read_fork_config_from_argv什么也不做，G_FORK_RECORDS为空。
        read_fork_config_from_argv(); 

        // c. 记录日志，标记主文件开始
        FILE * pFile2 = fopen(outlog, "a");
        if (pFile2) {
            if (G_FORK_RECORD_COUNT > 0) {
                 fprintf(pFile2, "\n[%5d]: forked process starting analysis on: %s \n", pid, name_main_file);
            } else {
                 fprintf(pFile2, "\n[%5d]: main process starting analysis on: %s \n", pid, name_main_file);
            }
            fclose(pFile2);
        }

        main_env_initialized = 1; // 确保只执行一次
    }


}

#endif /* Py_INSTRUMENT_H */











