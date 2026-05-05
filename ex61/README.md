# Sv39 Paging & Higher-half Kernel Booting

## Exercise 6 Slide
[OSC2026_lab6_exercise](<https://docs.google.com/presentation/d/1Wk0pZSu1GzPIp7RZP5O7sRoKsNXBfRnAV0kWlm3IqU4/edit?usp=sharing>)


## Introduction
In this exercise, we will explore virtual memory management on the RISC-V architecture by implementing a bare-metal **Higher-half Kernel**. You will learn how to configure the **Sv39 Paging System**, set up multi-level page tables, and safely transition the CPU execution state from physical addresses to high virtual addresses.

The ultimate goal is to map the physical RAM and MMIO devices (like UART) to the upper half of the virtual address space (`0xffffffc000000000`), switch the CPU into virtual memory mode, and successfully drop the initial transition mapping without crashing the system.

## Learning Objectives
1. Understand the Sv39 virtual memory layout and the translation process from Virtual Address (VA) to Physical Address (PA).
2. Learn how to configure Page Global Directory (PGD) and Page Middle Directory (PMD) for large pages (2MB/1GB).
3. Safely transition from physical to virtual execution using an Identity Mapping technique.
4. Grasp the importance of mapping Memory-Mapped I/O (MMIO) to virtual addresses when paging is enabled.

## TODOs

You need to complete the missing code blocks marked with `// TODO` in the source files.

### 1. `setup_vm()` (in `main.c`)
Initialize the page tables and configure the `satp` register to enable the MMU:
* **Kernel & Identity Mapping:** Configure 4GB of physical RAM (which starts from `0x80000000` on the QEMU `virt` machine). Map it simultaneously to its identical low physical address (Identity Mapping) and the target high virtual address (`0xffffffc080000000`).
* **UART (MMIO) Mapping:** Map the UART base physical address (`0x10000000`) to the high virtual address space (`0xffffffc010000000`) so the system can still perform I/O operations after paging is enabled.
* **Enable MMU:** Construct the Sv39 `satp` value using the physical address of your PGD, flush the TLB with `sfence.vma`, and write to the `satp` CSR.

### 2. `drop_identity_map()` (in `main.c`)
Clean up the transition mapping:
* Once the CPU is safely executing in the high virtual address space, clear the PGD entries associated with the lower identity mapping (`0x80000000` to `0x80000000`).
* Issue an `sfence.vma` instruction to flush the outdated translation cache.

### 3. Assembly Transition (in `boot.S`)
Adjust the execution flow and pointers:
* **Jump to High Address:** After returning from `setup_vm`, calculate the high virtual address of the next instruction (e.g., using a local label `1f` and adding `PAGE_OFFSET`) and use `jr` to jump to it.
* **Update Stack Pointer:** Once executing at the high address, update the stack pointer (`sp`) to use the virtual address of the stack (e.g., loading the virtual address of `_end`).

## Verification

Build and run the kernel using the following command:
```bash
make run
```

## Expected Result
> **Note:** The printed address shows the memory location of the `start_kernel` function. Because the `_start` boot code occupies the very beginning of the kernel space, the printed address will be slightly higher than the base address `0xffffffc080200000`.
```
Starting kernel at : 0xffffffc0802002e2
hello
osc
```