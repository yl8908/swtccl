#ifndef TCCL_UTILS_H
#define TCCL_UTILS_H

#include "tccl.h"
#include "alloc.h"
#include "checks.h"
#include <unordered_map>
#include <string>

struct netIf {
    char prefix[64];
    int port;
};
extern std::unordered_map<std::string, std::string> envMap;

int parseStringList(const char* string, struct netIf* ifList, int maxList);
bool matchIfList(const char* string, int port, struct netIf* ifList, int listSize, bool matchExact);
uint64_t getPid(void);
void getEnvFromFile();

#endif

