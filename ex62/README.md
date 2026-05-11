# Page Table Walk & User Mode Execution

## Exercise 6 Slide
[OSC2026_lab6_exercise](https://docs.google.com/presentation/d/1Wk0pZSu1GzPIp7RZP5O7sRoKsNXBfRnAV0kWlm3IqU4/edit?usp=sharing)

## Introduction
In this exercise, we will advance our virtual memory management by implementing fine-grained 4KB page mappings and successfully transitioning the system into **User Mode (U-Mode)**. You will learn how to configure the necessary execution environment.

The ultimate goal is to dynamically traverse the Sv39 3-level page tables to map the user program and its stack, drop CPU privileges from Supervisor Mode (S-Mode) to User Mode, and handle basic system traps triggered by the user application.

## Learning Objectives
1. Implement a full 3-level Sv39 page table walk (`pagewalk`) to allocate and map 4KB pages on demand.


## TODOs

You need to complete the missing code blocks marked with `// TODO` in the source files.

### 1. `pagewalk()`
Implement the 3-level Sv39 page table traversal:
* **Level Traverse:** Start from the Root Table (`pg_dir`) and extract the corresponding VPN segments (VPN[2], VPN[1], VPN[0]) from the virtual address to index into the page tables.
* **On-Demand Allocation:** Check the `PAGE_PRESENT` bit at Level 1 and Level 0. If the intermediate table does not exist, use `alloc_page()` to request a 4KB memory chunk, zero it out using `memset`, and link it to the current PTE with `PAGE_PRESENT` permissions.
* **Leaf Mapping:** Once you reach the bottom level (Level 0), insert the calculated Page Frame Number (PFN) of the target physical address and apply the requested protection flags (`prot`).


## Verification

Build and run the kernel using the following command:
```bash
make run
```

## Expected Result
```
Starting kernel ...
sepc: 0x0000000000000002, scause: 0x0000000000000008
sepc: 0x0000000000000002, scause: 0x0000000000000008
sepc: 0x0000000000000002, scause: 0x0000000000000008
sepc: 0x0000000000000002, scause: 0x0000000000000008
sepc: 0x0000000000000002, scause: 0x0000000000000008
```