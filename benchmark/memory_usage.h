#pragma once

#include <cstddef> // for size_t
#include <cstdint>

#if defined(_WIN32)
    #include <windows.h>
    #include <psapi.h>

    size_t getMemoryUsageBytes() {
        PROCESS_MEMORY_COUNTERS info;
        if (GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info))) {
            return static_cast<size_t>(info.WorkingSetSize);
        }
        return 0;
    }

#elif defined(__APPLE__) && defined(__MACH__)
    #include <sys/resource.h>

    size_t getMemoryUsageBytes() {
        struct rusage usage;
        if (getrusage(RUSAGE_SELF, &usage) == 0) {
            return static_cast<size_t>(usage.ru_maxrss);
        }
        return 0;
    }

#elif defined(__linux__)
    #include <fstream>
    #include <sstream>
    #include <string>

    size_t getMemoryUsageBytes() {
        std::ifstream status_file("/proc/self/status");
        std::string line;
        while (std::getline(status_file, line)) {
            if (line.substr(0, 6) == "VmRSS:") {  // Resident Set Size
                std::istringstream iss(line);
                std::string key;
                size_t value_kb;
                std::string unit;
                iss >> key >> value_kb >> unit;
                return value_kb * 1024; // Convert from KB to bytes
            }
        }
        return 0;
    }

#else
    size_t getMemoryUsageBytes() {
        // Unsupported platform
        return 0;
    }
#endif

