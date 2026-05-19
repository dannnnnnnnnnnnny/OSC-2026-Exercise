/*
 * ex62 boot/trap assembly。
 *
 * 這份檔案負責兩個階段：
 * 1. _start：設定最早期的 stack、啟用 Sv39、清空 .bss，最後跳到 C 端
 *    start_kernel()。
 * 2. handle_exception：處理 S-mode trap，保存完整暫存器狀態，呼叫 C 端
 *    do_trap()，再還原狀態並用 sret 回到 trap 前的位置。
 */

.section ".text.boot"
.global _start
_start:
    /*
     * linker 把 kernel 連結在 high-half 位址，但 QEMU 一開始從實體位址
     * 0x80200000 附近執行 kernel。setup_vm() 啟用 MMU 後，原本的低位址
     * return address 指令沒有建立 identity mapping，會觸發一次 trap。
     * (pgd 只有建立 high-half mapping -> low-half 沒有 valid PTE)
     *
     * 因此這裡預先把 stvec 設成 high-half 的 label 1。MMU 開啟後若因
     * 低位址取指而 trap，CPU 會跳到可映射的 high-half label 1 繼續。
     */
    la a2, 1f
    li t0, 0xffffffc000000000
    add a2, a2, t0
    csrw stvec, a2

    /* setup_vm() 是 C function，需要先準備一個可用的 kernel stack。 */
    la sp, _end
    call setup_vm

.align 2
1:
    /*
     * 來到這裡代表已經能用 high-half 虛擬位址執行。
     * 清空 .bss，讓未初始化的全域變數符合 C 語言預期的 0 初值。
     */
    la a3, __bss_start
    la a4, __bss_stop
    ble a4, a3, clear_bss_done
clear_bss:
    /* 每次清 8 bytes，直到 a3 追上 __bss_stop。 */
    sd zero, (a3)
    add a3, a3, 8
    blt a3, a4, clear_bss
clear_bss_done:
    /*
     * kernel 正式啟動後，所有 S-mode trap 都交給 handle_exception。
     * 此時 stvec 使用正常的 high-half handler 位址。
     */
    la a2, handle_exception
    csrw stvec, a2

    /* 重新設定 kernel stack，然後 tail call 到 C 端入口。 */
    la sp, _end
    tail start_kernel

.global handle_exception
.align 2
handle_exception:
    /*
     * trap 入口的第一件事是切換 stack。
     *
     * 從 U-mode trap 進來時：
     *   sp       = user stack pointer
     *   sscratch = kernel stack pointer，這是在 exec() 進 U-mode 前放好的
     *
     * csrrw sp, sscratch, sp 會交換 sp 與 sscratch：
     *   sp       變成 kernel stack pointer
     *   sscratch 變成 user stack pointer
     *
     * 從 S-mode trap 進來時，sscratch 通常是 0。交換後 sp 會變成 0，
     * 因此用 bnez 判斷是否真的拿到 kernel stack；若沒有，就從 sscratch
     * 取回原本的 S-mode sp。
     */
    csrrw sp, sscratch, sp
    bnez sp, save_context

    csrr sp, sscratch
save_context:
    /*
     * 在 kernel stack 上保存完整 context。
     *
     * 這裡配置 35 個 unsigned long：
     *   0..30：一般整數暫存器，順序對應 main.c 的 struct pt_regs。
     *   31   ：sepc，trap 發生時的 PC。
     *   32   ：sstatus，包含 SPP/SPIE 等返回狀態。
     *   33   ：scause，trap 原因。
     *   34   ：stval，page fault 位址或其他 trap 附加資訊。
     *
     * x0 永遠是 0，不需要保存。
     */
    addi sp, sp, -8 * 35
    sd ra,  8 *  0(sp)
    sd gp,  8 *  2(sp)
    sd tp,  8 *  3(sp)
    sd t0,  8 *  4(sp)
    sd t1,  8 *  5(sp)
    sd t2,  8 *  6(sp)
    sd s0,  8 *  7(sp)
    sd s1,  8 *  8(sp)
    sd a0,  8 *  9(sp)
    sd a1,  8 * 10(sp)
    sd a2,  8 * 11(sp)
    sd a3,  8 * 12(sp)
    sd a4,  8 * 13(sp)
    sd a5,  8 * 14(sp)
    sd a6,  8 * 15(sp)
    sd a7,  8 * 16(sp)
    sd s2,  8 * 17(sp)
    sd s3,  8 * 18(sp)
    sd s4,  8 * 19(sp)
    sd s5,  8 * 20(sp)
    sd s6,  8 * 21(sp)
    sd s7,  8 * 22(sp)
    sd s8,  8 * 23(sp)
    sd s9,  8 * 24(sp)
    sd s10, 8 * 25(sp)
    sd s11, 8 * 26(sp)
    sd t3,  8 * 27(sp)
    sd t4,  8 * 28(sp)
    sd t5,  8 * 29(sp)
    sd t6,  8 * 30(sp)

    /* 讀出 CSR 狀態，等等也保存到 pt_regs。 */
    csrr s0, sscratch
    csrr s1, sepc
    csrr s2, sstatus
    csrr s3, scause
    csrr s4, stval

    /* offset 1 的 sp 欄位保存 trap 前的 sp，也就是剛才暫存在 sscratch 的值。 */
    sd s0,  8 *  1(sp)
    sd s1,  8 * 31(sp)
    sd s2,  8 * 32(sp)
    sd s3,  8 * 33(sp)
    sd s4,  8 * 34(sp)

    /*
     * 清空 sscratch，表示現在已在 kernel trap handler 中。
     * 如果 do_trap() 期間又發生 S-mode trap，入口邏輯就會走 S-mode 分支。
     */
    csrw sscratch, zero

    /* a0 = struct pt_regs*，呼叫 C 端 do_trap() 處理並可能修改 epc。 */
    move a0, sp
    call do_trap

ret_from_exception:
    /*
     * 檢查保存的 sstatus.SPP。
     *
     * SPP=0 表示 trap 前在 U-mode。返回前需要把目前 kernel stack top
     * 放回 sscratch，下一次 U-mode trap 才能再次交換回 kernel stack。
     *
     * SPP=1 表示 trap 前在 S-mode，不需要為 user/kernel stack 做交換。
     */
    ld s0,  8 * 32(sp)
    andi s0, s0, (1 << 8)
    bnez s0, 1f

    addi s0, sp, 8 * 35
    csrw sscratch, s0
1:
    /* 還原 sepc/sstatus；sret 會依照這兩個 CSR 決定回到哪裡與哪個 mode。 */
    ld s1,  8 * 31(sp)
    ld s2,  8 * 32(sp)
    csrw sepc, s1
    csrw sstatus, s2

    /* 還原一般暫存器，順序必須和上方保存時的 offset 一致。 */
    ld ra,  8 *  0(sp)
    ld gp,  8 *  2(sp)
    ld tp,  8 *  3(sp)
    ld t0,  8 *  4(sp)
    ld t1,  8 *  5(sp)
    ld t2,  8 *  6(sp)
    ld s0,  8 *  7(sp)
    ld s1,  8 *  8(sp)
    ld a0,  8 *  9(sp)
    ld a1,  8 * 10(sp)
    ld a2,  8 * 11(sp)
    ld a3,  8 * 12(sp)
    ld a4,  8 * 13(sp)
    ld a5,  8 * 14(sp)
    ld a6,  8 * 15(sp)
    ld a7,  8 * 16(sp)
    ld s2,  8 * 17(sp)
    ld s3,  8 * 18(sp)
    ld s4,  8 * 19(sp)
    ld s5,  8 * 20(sp)
    ld s6,  8 * 21(sp)
    ld s7,  8 * 22(sp)
    ld s8,  8 * 23(sp)
    ld s9,  8 * 24(sp)
    ld s10, 8 * 25(sp)
    ld s11, 8 * 26(sp)
    ld t3,  8 * 27(sp)
    ld t4,  8 * 28(sp)
    ld t5,  8 * 29(sp)
    ld t6,  8 * 30(sp)

    /* 最後恢復 trap 前的 sp，再由 sret 回到 sepc。 */
    ld sp,  8 *  1(sp)
    sret
