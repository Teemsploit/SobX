#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>
#include <dlfcn.h>

// --- Configuration ---
typedef void (*GameSendChatFunc)(const char* msg, int type);
#define LIB_NAME "libroblox.so"

// --- Helper: Get Module Info ---
typedef struct {
    unsigned long start;
    unsigned long end;
} ModuleRegion;

ModuleRegion get_module_bounds(const char* name) {
    ModuleRegion region = {0, 0};
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return region;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, name) && strstr(line, "r-xp")) { // Look for executable segment
            unsigned long s, e;
            sscanf(line, "%lx-%lx", &s, &e);
            if (region.start == 0) region.start = s;
            region.end = e;
        }
    }
    fclose(f);
    return region;
}

// --- Pattern Scanning Engine ---
// Scans for a hex pattern like "48 89 5C 24 ? 57 48 83 EC"
unsigned long find_pattern(const char* module_name, const char* pattern) {
    ModuleRegion mod = get_module_bounds(module_name);
    if (mod.start == 0) return 0;

    // Convert pattern string to bytes
    size_t pat_len = strlen(pattern);
    unsigned char* pat_bytes = malloc(pat_len);
    unsigned char* pat_mask = malloc(pat_len);
    size_t real_len = 0;

    for (size_t i = 0; i < pat_len; i++) {
        if (pattern[i] == ' ') continue;
        if (pattern[i] == '?') {
            pat_bytes[real_len] = 0;
            pat_mask[real_len] = 0; // Wildcard
            real_len++;
            if (pattern[i+1] == '?') i++; 
        } else {
            pat_bytes[real_len] = (unsigned char)strtol(&pattern[i], NULL, 16);
            pat_mask[real_len] = 1;
            real_len++;
            i++; 
        }
    }

    // Actual Scan
    for (unsigned long i = mod.start; i < mod.end - real_len; i++) {
        int found = 1;
        for (size_t j = 0; j < real_len; j++) {
            if (pat_mask[j] && *(unsigned char*)(i + j) != pat_bytes[j]) {
                found = 0;
                break;
            }
        }
        if (found) {
            free(pat_bytes);
            free(pat_mask);
            return i;
        }
    }

    free(pat_bytes);
    free(pat_mask);
    return 0;
}

// --- Logic ---

void send_game_chat(const char* message) {
    // Example signature for a chat function (this is a placeholder, you'd find the real one via Ghidra/IDA)
    static unsigned long chat_func_addr = 0;
    if (chat_func_addr == 0) {
        chat_func_addr = find_pattern(LIB_NAME, "48 89 5C 24 ? 55 48 8D 6C 24 ? 48 81 EC");
    }

    if (chat_func_addr) {
        GameSendChatFunc send_chat = (GameSendChatFunc)chat_func_addr;
        send_chat(message, 0);
    } else {
        printf("[Atingle] Function signature not found!\n");
    }
}

void* main_thread(void* lpParam) {
    printf("[Atingle] Internal Thread Started. Waiting for Game Init...\n");
    sleep(5); // Give the game time to load libroblox.so
    
    send_game_chat("Atingle System: Initialization Complete");
    return NULL;
}

// --- Entry Point ---
__attribute__((constructor))
void init_lib() {
    // Run in a separate thread so we don't freeze the process during load
    pthread_t t;
    pthread_create(&t, NULL, main_thread, NULL);
    pthread_detach(t);
}
