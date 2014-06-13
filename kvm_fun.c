#include <linux/kvm.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <stdint.h>

#define KVM_FILE "/dev/kvm"
#define MB (1024*1024)

struct kvm_attr 
{
    int kvm_version;
    int kvm_fd;
    int vm_fd;
    int vcpu_fd;
};

void init_kvm_attr(struct kvm_attr *kap)
{
    kap->kvm_version = 0;
    kap->kvm_fd = -1;
    kap->vm_fd = -1;
    kap->vcpu_fd = -1;
}

/* start in protected mode */
void vcpu_pm_mode(struct vcpu *vcpu) 
{
    struct kvm_sregs sregs;
    vcpu_get_sregs(vcpu, sregs);

    /* setup basic flat model */
    sregs.cs.base = 0x0; sregs.cs.limit = 0xffffffff; sregs.cs.g = 1;
    sregs.ds.base = 0x0; sregs.ds.limit = 0xffffffff; sregs.ds.g = 1;
    sregs.ss.base = 0x0; sregs.ss.limit = 0xffffffff; sregs.ss.g = 1;
    
    /* set default operation size and stack pointer size to 32-bit */
    sregs.cs.db = 1;
    sregs.ss.db = 1;

    /* activate PM bit in cr0 */
    sregs.cr0 |= 0x01;
    
    vcpu_set_sregs(vcpu, sregs);
}

int main()
{
    struct kvm_attr *kap = (struct kvm_attr *)malloc(sizeof(struct kvm_attr));
    unsigned int ch;

    init_kvm_attr(kap);

    /* open /dev/kvm and get kvm fd */
    kap->kvm_fd = open(KVM_FILE, O_RDWR|O_NONBLOCK);
    if (kap->kvm_fd == -1)
        return -1;

    kap->kvm_version = ioctl(kap->kvm_fd, KVM_GET_API_VERSION, 0);
    if (12 != kap->kvm_version) {
        fprintf(stderr, "kvm version[%d] is not stable, please update it\n", kap->kvm_version);
        exit(errno);
    }

    /* create vm fd */
    kap->vm_fd = ioctl(kap->kvm_fd, KVM_CREATE_VM, 0);
    
    /* add space for some strange reason on intel (3 pages) */
    ioctl(kap->vm_fd, KVM_SET_TSS_ADDR, 0xffffffffffffd000);
    ioctl(kap->vm_fd, KVM_CREATE_IRQCHIP, 0);

    /* set memory region */
    void *addr = mmap(NULL, 10 * MB, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

    struct kvm_userspace_memory_region region = {
        .slot = 0,
        .flags = 0, /* can be read only */
        .guest_phys_addr = 0x100000,
        .memory_size = 10 * MB,
        .userspace_addr = (__u64)addr
    };

    ioctl(kap->vm_fd, KVM_SET_MEMORY_REGION, &region);

    /* create a vcpu and initialization */
    kap->vcpu_fd = ioctl(kap->vm_fd, KVM_CREATE_VCPU, 0);

    struct kvm_regs regs;
    ioctl(kap->vcpu_fd, KVM_GET_REGS, &regs);
    regs.rflags = 0x02;
    regs.rip = 0x0100f000;
    ioctl(kap->vcpu_fd, KVM_SET_REGS, &regs);

    /* running a vcpu */
    int kvm_run_size = ioctl(kap->kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
    struct kvm_run *run_stat = 
        mmap(NULL, kvm_run_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, kap->vcpu_fd, 0);

    for (; ;) {
        int ret = ioctl(kap->vcpu_fd, KVM_RUN, 0);
        switch (run_stat->exit_reason) {
            /* use run_stat to gather informations about teh exit */

            case KVM_EXIT_UNKNOWN:
            case KVM_EXIT_EXCEPTION:
            case KVM_EXIT_HYPERCALL:
                exit(0);
            case KVM_EXIT_DEBUG:
                printf("Debugging...\n");
                break;
            case KVM_EXIT_HLT:
            case KVM_EXIT_IRQ_WINDOW_OPEN:
            case KVM_EXIT_SHUTDOWN:
            case KVM_EXIT_FAIL_ENTRY:
            case KVM_EXIT_INTR:
            case KVM_EXIT_SET_TPR:
            case KVM_EXIT_TPR_ACCESS:
            case KVM_EXIT_S390_SIEIC:
            case KVM_EXIT_S390_RESET:
            case KVM_EXIT_DCR:
            case KVM_EXIT_NMI:
            case KVM_EXIT_INTERNAL_ERROR:
                exit(0);
                /* Devices: PIO */
            case KVM_EXIT_IO:
                switch(run_stat->io.direction) {
                    case KVM_EXIT_IO_IN:
                        printf("Enter value to send: ");
                        scanf("%u", &ch);
                        *((uint8_t *)run_stat + run_stat->io.data_offset) = ch;
                        printf("SENT: %d\n", (unsigned int)(unsigned char)ch);
                        break;
                    case KVM_EXIT_IO_OUT:
                        printf("RECEIVED: %d\n", *((uint8_t *)run_stat + run_stat->io.data_offset));
                        break;
                        break;

                        /*
                           if (run_stat->io.port == CONSOLE_PORT && run_stat->io.direction == KVM_EXIT_IO_OUT) {
                           __u64 offset = run_stat->io.data_offset;
                           __u32 size = run_stat->io.size;
                           write(STDOUT_FILENO, (char *)run_stat + offset, size);
                           }
                           break;
                           */

                        /* Devices: MMIO */
                    case KVM_EXIT_MMIO:

                    default:

                }
        }
    }

    return 0;
}
