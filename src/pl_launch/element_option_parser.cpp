#include <filesystem>
#define PL_LOG_ID PL_LOG_LANUCH
#include "element_option_parser.h"

std::filesystem::path CONFIG_PATH;

void set_config_path(const char* config_path) { CONFIG_PATH = config_path; }

int element_option_parser(char* argv[], int argcIndex, int argc, ElementParam* param) {
    for (; argcIndex < argc; ++argcIndex) {
        if (!strcmp(argv[argcIndex], "-name")) {
            param->name = argv[++argcIndex];
        } else if (!strcmp(argv[argcIndex], "-path")) {
            param->path = (CONFIG_PATH / argv[++argcIndex]).string();
        } else if (!strcmp(argv[argcIndex], "-timeout")) {
            param->timeout = atoi(argv[++argcIndex]);
        } else if (!strcmp(argv[argcIndex], "-loopnum")) {
            param->loopnum = atoi(argv[++argcIndex]);
        } else if (!strcmp(argv[argcIndex], "-fps")) {
            param->fps = atoi(argv[++argcIndex]);
        } else if (!strcmp(argv[argcIndex], "-deepth") || !strcmp(argv[argcIndex], "-depth")) {
            param->depth = atoi(argv[++argcIndex]);
        } else if (!strcmp(argv[argcIndex], "-type")) {
            param->type = atoi(argv[++argcIndex]);
        } else if (!strcmp(argv[argcIndex], "-maxFrameRate")) {
            param->maxFrameRate = atoi(argv[++argcIndex]);
        } else if (!strcmp(argv[argcIndex], "-timeWnd")) {
            param->timeWnd = atoi(argv[++argcIndex]);
        } else if (!strcmp(argv[argcIndex], "-ipcCapacity")) {
            param->ipcCapacity = atoi(argv[++argcIndex]);
        } else if (!strcmp(argv[argcIndex], "-startPad")) {
            param->startPadIndex = atoi(argv[++argcIndex]);
        } else if (!strcmp(argv[argcIndex], "-poolsize")) {
            param->muxPoolSize = atoi(argv[++argcIndex]);
        } else if (!strcmp(argv[argcIndex], "-elementType")) {
            param->elementType = static_cast<BASE_ELEMENT_TYPE>(atoi(argv[++argcIndex]));
        } else if (!strcmp(argv[argcIndex], "-")) {
            argcIndex++;
            break;
        } else if (!strcmp(argv[argcIndex], "-die")) {
            int dieIndex = atoi(argv[++argcIndex]);
            param->dieIndex = std::min(std::max(dieIndex, 0), 1);
        } else if (!strcmp(argv[argcIndex], "!")) {
            break;
        }
    }
    return argcIndex;
}
