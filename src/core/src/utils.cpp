#define PL_LOG_ID PL_LOG_CORE
#include <unistd.h>

#include "utilities.h"
#define MAX_PATH_LEN 256
using namespace std;

// zlog_category_t *mAppxCat = nullptr;

int createFile(const string &directoryPath, ios::openmode mode) {
    ofstream oFile;
    oFile.open(directoryPath, mode);
    if (!oFile) {
        printf("create file failed, %s\n", directoryPath.c_str());
        return APP_FAILURE;
    } else {
        oFile.close();
        return APP_SUCCESS;
    }
}

int createDir(const string &directoryPath, mode_t mode) {
    uint32_t dirPathLen = directoryPath.length();
    if (dirPathLen > MAX_PATH_LEN) {
        return APP_FAILURE;
    }

    int ret;
    char tmpDirPath[MAX_PATH_LEN] = {0};
    for (uint32_t i = 0; i < dirPathLen; ++i) {
        tmpDirPath[i] = directoryPath[i];
        if (tmpDirPath[i] == '\\' || tmpDirPath[i] == '/') {
            if (access(tmpDirPath, 0) != 0) {
                ret = mkdir(tmpDirPath, mode);
                if (ret != 0) {
                    printf("mkdir failed, %s\n", directoryPath.c_str());
                    return APP_FAILURE;
                }
            }
        }
    }

    return APP_SUCCESS;
}