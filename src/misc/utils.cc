/*************************************************************************
 * Copyright (c) 2016-2020, Tecorigin CORPORATION. All rights reserved.
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>
#include "utils.h"
#include "log.h"

std::unordered_map<std::string, std::string> envMap;

int parseStringList(const char* string, struct netIf* ifList, int maxList) {
    if (!string) return 0;

    const char* ptr = string;

    int ifNum = 0;
    int ifC = 0;
    char c;
    do {
        c = *ptr;
        if (c == ':') {
            if (ifC > 0) {
                ifList[ifNum].prefix[ifC] = '\0';
                ifList[ifNum].port = atoi(ptr+1);
                ifNum++; ifC = 0;
            }
            while (c != ',' && c != '\0') c = *(++ptr);
        } else if (c == ',' || c == '\0') {
            if (ifC > 0) {
                ifList[ifNum].prefix[ifC] = '\0';
                ifList[ifNum].port = -1;
                ifNum++; ifC = 0;
            }
        } else {
            ifList[ifNum].prefix[ifC] = c;
            ifC++;
        }
        ptr++;
    } while (ifNum < maxList && c);
    return ifNum;
}

static bool matchIf(const char* string, const char* ref, bool matchExact) {
    // Make sure to include '\0' in the exact case
    int matchLen = matchExact ? strlen(string) + 1 : strlen(ref);
    return strncmp(string, ref, matchLen) == 0;
}

static bool matchPort(const int port1, const int port2) {
    if (port1 == -1) return true;
    if (port2 == -1) return true;
    if (port1 == port2) return true;
    return false;
}


bool matchIfList(const char* string, int port, struct netIf* ifList, int listSize, bool matchExact) {
    // Make an exception for the case where no user list is defined
    if (listSize == 0) return true;

    for (int i = 0; i < listSize; i++) {
        if (matchIf(string, ifList[i].prefix, matchExact)
            && matchPort(port, ifList[i].port)) {
            return true;
        }
    }
    return false;
}

uint64_t getPid(void) {
    return (long) getpid();
}

void getEnvFromFile() {
    struct passwd *pw = getpwuid(getuid()); // NOLINT
    char* homePath = pw == NULL ? NULL : pw->pw_dir;
    if (homePath == NULL) return;
    char envFilePath[256];
    snprintf(envFilePath, sizeof(envFilePath), "%s/.tccl.conf", homePath);
    FILE * file = fopen(envFilePath, "r");
    if (file == NULL) return;

    char *line = NULL;
    char env[256];
    char value[256];
    size_t buffSize = 0;
    ssize_t lineLen;
    while ((lineLen = getline(&line, &buffSize, file)) != -1) {
        if (line[lineLen-1] == '\n') line[lineLen-1] = '\0';
        int s = 0;
        while (line[s] != '\0' && line[s] != '=') s++;
        if (line[s] == '\0') continue;
        strncpy(env, line, s);
        env[s] = '\0';
        s++;
        strncpy(value, line+s, lineLen-s);
        envMap[env] = value;
    }
    if (line) free(line);
    fclose(file);
}

