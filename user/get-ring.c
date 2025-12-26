#include <stdio.h>
#include <stdint.h>

void check_privilege_level() {
    uint16_t cs_reg;

    // 内联汇编读取 CS
    __asm__ __volatile__("mov %%cs, %0" : "=r"(cs_reg));

    printf("Current CS Selector: 0x%x\n", cs_reg);

    // CPL (Current Privilege Level) 是选择子的最低 2 位
    int cpl = cs_reg & 3;

    if (cpl == 0) {
        printf("I am in RING 0 (Kernel Mode)! DANGER!\n");
    } else if (cpl == 1) {
        printf("I am in RING 1 (Service Mode)!\n");
    } else if (cpl == 3) {
        printf("I am in RING 3 (User Mode). Safe and Sound.\n");
    } else {
        printf("Unknown Ring Level: %d\n", cpl);
    }
}

int main() {
    check_privilege_level();
    return 0;
}
