/* Kernel stacks — shared across arches. */
#include <kernel.h>

__attribute__((aligned(16)))
u8 kernel_stack[64 * KB];

u8 *kernel_stack_top = kernel_stack + sizeof(kernel_stack);
