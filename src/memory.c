#include <stdio.h>
#include <stdlib.h>

#if defined(__APPLE__)
#include <mach/mach.h>

size_t get_memory_usage_bytes(void)
{
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t info_count = MACH_TASK_BASIC_INFO_COUNT;
    const kern_return_t kr = task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                                       (task_info_t)&info, &info_count);
    if (kr != KERN_SUCCESS) {
        return 0;
    }
    return info.virtual_size;
}

size_t get_private_memory_usage_bytes(void)
{
    task_vm_info_data_t vm_info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vm_info,
                  &count) != KERN_SUCCESS)
        return 0;

    return vm_info.internal;
}

#elif defined(__linux__)
#include <sys/resource.h>
#include <unistd.h>

size_t get_memory_usage_bytes(void)
{
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return (size_t)usage.ru_maxrss * 1024L; // ru_maxrss is in KB
}

size_t get_private_memory_usage_bytes(void)
{
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return (size_t)usage.ru_maxrss * 1024L; // ru_maxrss is in KB
}

#else
size_t get_memory_usage_bytes(void)
{
    return 0; // Unsupported platform
}
#endif
