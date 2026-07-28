#define PL_LOG_ID PL_LOG_CORE
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <unistd.h>

#include <chrono>
#include <iostream>
#include <thread>
#define MAX 1024

using namespace std;
// get current process pid
inline int GetCurrentPid() { return getpid(); }

// get specific process cpu occupation ratio by pid

// FIXME: can also get cpu and mem status from popen cmd
// the info line num in /proc/{pid}/status file
#define VMRSS_LINE 22
#define VMSIZE_LIEN 18
#define PROCESS_ITEM 14
char* temp_p;
static const char* get_items(const char* buffer, unsigned int item) {
    // read from buffer by offset
    const char* p = buffer;

    int len = strlen(buffer);
    int count = 0;

    for (int i = 0; i < len; i++) {
        if (' ' == *p) {
            count++;
            if (count == item - 1) {
                p++;
                break;
            }
        }
        p++;
    }

    return p;
}

static inline unsigned long get_cpu_total_occupy() {
    // get total cpu use time

    // different mode cpu occupy time
    unsigned long user_time;
    unsigned long nice_time;
    unsigned long system_time;
    unsigned long idle_time;

    FILE* fd;
    char buff[1024] = {0};

    fd = fopen("/proc/stat", "r");
    if (nullptr == fd) return 0;

    temp_p = fgets(buff, sizeof(buff), fd);
    char name[64] = {0};
    sscanf(buff, "%s %ld %ld %ld %ld", name, &user_time, &nice_time, &system_time, &idle_time);
    fclose(fd);

    return (user_time + nice_time + system_time + idle_time);
}

static inline unsigned long get_cpu_proc_occupy(int pid) {
    // get specific pid cpu use time
    unsigned int tmp_pid;
    unsigned long utime;   // user time
    unsigned long stime;   // kernel time
    unsigned long cutime;  // all user time
    unsigned long cstime;  // all dead time

    char file_name[64] = {0};
    FILE* fd;
    char line_buff[1024] = {0};
    sprintf(file_name, "/proc/%d/stat", pid);

    fd = fopen(file_name, "r");
    if (nullptr == fd) return 0;

    temp_p = fgets(line_buff, sizeof(line_buff), fd);

    sscanf(line_buff, "%u", &tmp_pid);
    const char* q = get_items(line_buff, PROCESS_ITEM);
    sscanf(q, "%ld %ld %ld %ld", &utime, &stime, &cutime, &cstime);
    fclose(fd);

    return (utime + stime + cutime + cstime);
}

inline float GetCpuUsageRatio(int pid) {
    unsigned long totalcputime1, totalcputime2;
    unsigned long procputime1, procputime2;

    totalcputime1 = get_cpu_total_occupy();
    procputime1 = get_cpu_proc_occupy(pid);

    // FIXME: the 200ms is a magic number, works well
    usleep(20000);  // sleep 200ms to fetch two time point cpu usage snapshots
                    // sample for later calculation

    totalcputime2 = get_cpu_total_occupy();
    procputime2 = get_cpu_proc_occupy(pid);

    float pcpu = 0.0;
    if (0 != totalcputime2 - totalcputime1)
        pcpu = (procputime2 - procputime1) / float(totalcputime2 - totalcputime1);  // float number

    int cpu_num = get_nprocs();
    pcpu *= cpu_num;  // should multiply cpu num in multiple cpu machine

    return pcpu;
}

// get specific process physical memeory occupation size by pid (MB)
inline float GetMemoryUsage(int pid) {
    char file_name[64] = {0};
    FILE* fd;
    char line_buff[512] = {0};
    sprintf(file_name, "/proc/%d/status", pid);

    fd = fopen(file_name, "r");
    if (nullptr == fd) return 0;

    char name[64];
    int vmrss = 0;
    for (int i = 0; i < VMRSS_LINE - 1; i++) temp_p = fgets(line_buff, sizeof(line_buff), fd);

    temp_p = fgets(line_buff, sizeof(line_buff), fd);
    sscanf(line_buff, "%s %d", name, &vmrss);
    fclose(fd);

    // cnvert VmRSS from KB to MB
    return vmrss / 1024.0;
}

int getFpCount(char* root) {
    DIR* dir;
    struct dirent* ptr;
    int total = 0;

    dir = opendir(root); /* 打开目录*/
    if (dir == NULL) {
        perror("fail to open dir");
    } else {
        while ((ptr = readdir(dir)) != NULL) {
            // 顺序读取每一个目录项；
            // 跳过“..”和“.”两个目录
            if (strcmp(ptr->d_name, ".") == 0 || strcmp(ptr->d_name, "..") == 0) {
                continue;
            }

            if (ptr->d_type != DT_DIR) {
                total++;
                // printf("%s%s/n", root, ptr->d_name);
            }
        }
        closedir(dir);
    }

    return total;
}

int getCpuUsage() {
    int current_pid = GetCurrentPid();  // or you can set a outside program pid

    // /proc/pid/fd/
    char file_name[64] = {0};
    sprintf(file_name, "/proc/%d/fd/", current_pid);
    int fp_count = getFpCount(file_name);
    float cpu_usage_ratio = GetCpuUsageRatio(current_pid);
    // float memory_usage = GetMemoryUsage(current_pid);
    // std::cout << "current pid: " << current_pid << std::endl;
    // std::cout << "memory_usage MB: " << memory_usage << std::endl;
    // std::cout << "current_pid: " << current_pid << std::endl;
    std::cout << "fd count: " << fp_count << std::endl;
    std::cout << "cpu usage ratio: " << cpu_usage_ratio * 100 << "%" << std::endl;
    return 0;
}

void getCpuMemMMZ(string name = "", bool enableMemDetail = false) {
    // mmz
    printf("##################### CPU MEM MMZ ################### \n");
    printf("name: %s\n", name.c_str());
    FILE* fp = fopen("/proc/eswin/vb", "r");
    if (fp != NULL) {
        char line[128];
        while (fgets(line, 128, fp) != NULL) {
            string::size_type position;
            position = string(line).find("memblock:");
            if (position != string::npos) {
                printf("%s ", line);
            }
            // if (strncmp(line, "memblock:", 9) == 0)
            // {
            //     printf("%s ", line);
            //     break;
            // }
        }
        fclose(fp);
    } else {
        printf("文件不存在:/proc/eswin/vb \n");
    }

    // common mem
    fp = fopen("/proc/self/status", "r");
    if (fp != NULL) {
        char line[128];
        while (fgets(line, 128, fp) != NULL) {
            if (strncmp(line, "VmSize:", 7) == 0)  // line 18
            {
                printf("%s ", line);
            }
            if (strncmp(line, "VmRSS:", 6) == 0)  // line 22
            {
                printf("%s ", line);
                break;
            }
        }
        fclose(fp);
    } else {
        printf("文件不存在:/proc/self/status \n");
    }
    fflush(stdout);

    fp = fopen("/proc/meminfo", "r");
    if (fp != NULL) {
        char line[128];
        while (fgets(line, 128, fp) != NULL) {
            if (strncmp(line, "MemFree:", 8) == 0) {
                printf("%s ", line);
            }
            if (strncmp(line, "MemAvailable:", 13) == 0) {
                printf("%s ", line);
                break;
            }
        }
        fclose(fp);
    } else {
        printf("文件不存在:/proc/meminfo \n");
    }
    fflush(stdout);
    if (enableMemDetail) {
        fp = fopen("/sys/kernel/debug/dma_buf/bufinfo", "r");
        if (fp != NULL) {
            char line[128];
            while (fgets(line, 128, fp) != NULL) {
                printf("%s ", line);
            }
            fclose(fp);
        } else {
            printf("文件不存在:/sys/kernel/debug/dma_buf/bufinfo \n");
        }
        fflush(stdout);
    }

    fp = fopen("/proc/sys/fs/file-nr", "r");
    if (fp != NULL) {
        char line[128];
        while (fgets(line, 128, fp) != NULL) {
            printf("%s ", line);
        }
        fclose(fp);
    } else {
        printf("文件不存在:/proc/sys/fs/file-nr \n");
    }
    fflush(stdout);

    // cpu
    getCpuUsage();
    printf("##################### CPU MEM MMZ END ################### \n");
    fflush(stdout);
}