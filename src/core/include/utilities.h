#ifndef __es_lpl_common_utilities_h__
#define __es_lpl_common_utilities_h__

#include <stdint.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "error.h"
#include "video.h"

using namespace std;

double getCurrentTimeStamp(void);
string getCurrentDataAndTimeStr();

int createFile(const string &directoryPath, ios::openmode mode = ios::ate | ios::out);
int createDir(const string &directoryPath, mode_t mode = S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

void showImage(const CImage &image, const int delay, string winName);

#endif