#ifndef __es_lpl_common_error_h__
#define __es_lpl_common_error_h__

typedef int app_ret;

#define APP_SUCCESS 0
#define APP_FAILURE -1
#define APP_FILE_NOT_FOUNT -2
#define APP_VIDEO_OPEN_FAIL -3
#define APP_INVALID_CFG_VALUE -4
#define APP_INIT_FAILED -5
#define APP_EOS -6

#define ES_ASSERT(condition, fmt, ...)                                                       \
    do {                                                                                     \
        if (!(condition)) {                                                                  \
            printf("%s-%s-%d: " fmt, PLLOG_fileName(__FILE__), __func__, __LINE__, ##__VA_ARGS__); \
            int tmp[1], i = 0;                                                               \
            while (1) {                                                                      \
                tmp[i++] = 0;                                                                \
            }                                                                                \
        }                                                                                    \
    } while (0)

#endif
