.section ".text.boot"
.global _start

/*
 * 核心的第一個執行點。
 *
 * 這段程式在啟用 MMU 前就會被 CPU 執行，因此一開始使用的位址仍然是
 * 實體位址或 linker 目前能直接抵達的位址。流程大致如下：
 *
 *   1. 先放一個臨時 stack，讓 C 函式 setup_vm() 可以安全執行。
 *   2. setup_vm() 建立 Sv39 頁表，並同時保留 identity mapping 與
 *      PAGE_OFFSET 高位址映射。
 *   3. 開啟分頁後，手動跳到同一段程式碼的高位址版本。
 *   4. 重新設定 stack、清空 .bss、移除 identity mapping。
 *   5. 進入真正的 C kernel 入口 start_kernel()。
 */
_start:
    /*
     * 暫時把 stack pointer 設在 _end。
     *
     * _end 通常由 linker script 提供，代表 kernel 映像檔最後的位置。
     * 這裡先拿它當作簡單的早期 stack 使用，讓接下來的 C 函式呼叫
     * 有可用的堆疊空間。
     */
    la      sp, _end

    /*
     * 建立並啟用早期頁表。
     *
     * setup_vm() 會在 main.c 中填好 PGD/PMD，映射 RAM 與 UART，
     * 然後把頁表根位址寫進 satp，正式打開 Sv39 分頁。
     */
    call    setup_vm

    /*
     * MMU 開啟後，CPU 仍在這附近繼續執行。為了讓 kernel 之後全部
     * 運行在高位址區間，這裡計算 label 1 的高位址版本並跳過去。
     *
     * t0 = label 1 目前位址
     * t1 = PAGE_OFFSET，也就是 kernel high-half / linear map 的基底
     * t0 = label 1 + PAGE_OFFSET
     */
    la      t0, 1f
    li      t1, 0xffffffc000000000
    add     t0, t0, t1

    /*
     * 使用 jr 跳到高位址的 label 1。
     *
     * 這個跳轉能成功，是因為 setup_vm() 已經建立了
     * PAGE_OFFSET + 實體 RAM 的映射。
     */
    jr      t0

.align 2
1:
    /*
     * 進入高位址後重新載入 stack pointer。
     *
     * la 會依目前的位址環境重新產生 _end 的位址；此時我們已經在
     * 高位址映射中執行，因此 stack 也切到 kernel 高位址空間。
     */
    la      sp, _end

    /*
     * 清空 .bss 區段。
     *
     * C 語言要求未初始化的全域變數與 static 變數初始值為 0。
     * bootloader 載入 kernel 映像時不一定會幫我們清 .bss，
     * 所以 kernel 進入 C 主流程前必須自己完成這件事。
     */
    la      a3, __bss_start
    la      a4, __bss_stop

    /*
     * 若 __bss_stop <= __bss_start，代表沒有需要清除的範圍，
     * 直接跳過 clear_bss 迴圈。
     */
    ble     a4, a3, clear_bss_done

clear_bss:
    /*
     * 每次以 8 bytes 為單位寫入 0。
     *
     * sd 是 store doubleword；在 RV64 上剛好是 unsigned long 的大小。
     * linker 通常會讓 .bss 以足夠的對齊方式排列，因此這裡用 8 bytes
     * 逐步清除即可。
     */
    sd      zero, (a3)
    add     a3, a3, 8
    blt     a3, a4, clear_bss

clear_bss_done:
    /*
     * 此時程式與 stack 都已經在高位址執行，早期過渡用的
     * identity mapping 不再需要。移除它可以避免 kernel 後續意外
     * 透過低位址別名存取同一份實體記憶體。
     */
    call    drop_identity_map

    /*
     * tail 會跳到 start_kernel()，並且不保留回到 _start 的返回位址。
     *
     * start_kernel() 是真正的 C kernel 入口；一旦進入後就不應該回到
     * 這段早期啟動程式。
     */
    tail    start_kernel
