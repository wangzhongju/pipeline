#ifndef __element_option_parser_h__
#define __element_option_parser_h__

#include "element.h"

int element_option_parser(char* argv[], int argcIndex, int argc, ElementParam* param);

/**
 * @brief Set the config path object
 *
 * @param config_path
 */
void set_config_path(const char* config_path);

#endif