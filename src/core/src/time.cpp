#define PL_LOG_ID PL_LOG_CORE
#include "utilities.h"
using namespace std;

double getCurrentTimeStamp(void) {
    struct timeval t;
    gettimeofday(&t, 0);
    return t.tv_sec + 1E-6 * t.tv_usec;
}

string getCurrentDataAndTimeStr() {
    stringstream ts;
    string timeStr;
    time_t now = chrono::system_clock::to_time_t(chrono::system_clock::now());

    ts << put_time(localtime(&now), "%Y-%m-%d-%H-%M-%S");
    timeStr = ts.str();

    return timeStr;
}