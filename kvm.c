#include <linux/kvm.h>
#include <stdio.h>
#include <fcntl.h>
#include <malloc.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
 
int configureMemory(int kvmFD, char* buffer)
{
    int res;
    printf("Attempting to setup memory at host address %p\n",buffer);
    struct kvm_userspace_memory_region memory;
    memory.slot = 0;
    memory.flags = 0;
    memory.memory_size = 0x10000; // Must be a multiple of PAGE_SIZE
    memory.guest_phys_addr = 0xF0000; // Must be aligned to PAGE_SIZE
    memory.userspace_addr = (__u64) buffer;
    res = ioctl(kvmFD, KVM_SET_USER_MEMORY_REGION, &memory);
    printf("KVM_SET_USER_MEMORY_REGION result: %d\n",res);
}
 
int main()
{
    int res;
    int fd = open("/dev/kvm", O_RDWR);
    if(fd < 0) {
        printf("Can't open /dev/kvm\n");
        exit(1);
    }
    printf("/dev/kvm open with FD %d\n",fd);

    int apiVersion = ioctl(fd, KVM_GET_API_VERSION, 0);
    printf("KVM API version is %d\n",apiVersion);

    int kvmFD = ioctl(fd, KVM_CREATE_VM, 0);

    printf("Created a KVM FD: %d\n",kvmFD);

    // Try to create a vcpu
    int vcpuid = 0;
    int vcpuFD = ioctl(kvmFD, KVM_CREATE_VCPU, vcpuid);

    printf("KVM_CREATE_VCPU: %d\n",vcpuFD);
    if(vcpuFD == -1) {
        exit(1);
    }

    char* buffer;

    buffer = mmap(0, 65536, PROT_READ|PROT_WRITE|PROT_EXEC,
            MAP_SHARED | MAP_ANONYMOUS, 0, 0);
    if(buffer==MAP_FAILED) {
        printf("mmap of memory failed.\n");
        exit(2);
    }

    memset(buffer, 0x90, 65536); // Set all to NOP
    configureMemory(kvmFD, buffer);

    int mmapSize = ioctl(fd, KVM_GET_VCPU_MMAP_SIZE, 0);
    printf("VCPU mmap size is %d bytes\n",mmapSize);

    void* vcpuMap = mmap(0, mmapSize, PROT_READ|PROT_WRITE, MAP_SHARED,
            vcpuFD, 0);
    struct kvm_run *runData;
    runData = (struct kvm_run *) vcpuMap;

    struct kvm_regs regs;
    res = ioctl(vcpuFD, KVM_GET_REGS, &regs);
    if(res == 0) {
        printf("Starting IP: 0x%llx\n",regs.rip);
    }
    else
    {
        printf("Can't GET_REGS from VCPU\n");
        exit(1);
    }

    struct kvm_sregs sregs;
    res = ioctl(vcpuFD, KVM_GET_SREGS, &sregs);
    if(res == 0) {
        printf("Starting CS: Base 0x%llx Limit 0x%x Selector 0x%hx\n",
                sregs.cs.base, sregs.cs.limit, sregs.cs.selector);
    }
    else
    {
        printf("Can't GET_SREGS from VCPU\n");
        exit(1);
    }

    int stopRes = ioctl(vcpuFD, KVM_RUN, 0);
    printf("Stopped with code %d\n",stopRes);
    unsigned int stopReason = runData->exit_reason;
    printf("exit_reason %u\n",stopReason);

    if(stopReason == KVM_EXIT_INTERNAL_ERROR) {
        printf("Suberror code %d\n",runData->internal.suberror);
    }

    res = ioctl(vcpuFD, KVM_GET_REGS, &regs);
    if(res == 0) {
        printf("Finishing IP: 0x%llx\n",regs.rip);
    }
    else
    {
        printf("Can't GET_REGS from VCPU\n");
        exit(1);
    }

    munmap(vcpuMap, mmapSize);
    munmap(buffer, 65536);
    close(fd);
}
