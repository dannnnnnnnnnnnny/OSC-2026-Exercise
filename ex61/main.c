extern char uart_getc(void);
extern void uart_putc(char c);
extern void uart_puts(const char* s);
extern void uart_hex(unsigned long h);

/*
 * Memory map / 位址配置
 *
 * 這份練習使用 RISC-V Sv39 分頁。Sv39 的虛擬位址由三級頁表組成：
 *
 *   VPN[2] -> PGD：每格涵蓋 1 GiB
 *   VPN[1] -> PMD：每格涵蓋 2 MiB
 *   VPN[0] -> PTE：每格涵蓋 4 KiB
 *
 * 本檔案只建立到 PMD 層，並用 PMD leaf entry 做 2 MiB huge page 映射。
 * 這樣可以用較少的頁表項目映射整段 RAM，適合早期 kernel bootstrap。
 */
#define PAGE_OFFSET   0xffffffc000000000UL
#define PAGE_SIZE     (1UL << 12)
#define PMD_SIZE      (1UL << 21)  /* 2 MiB：VPN[0] 9 bits + page offset 12 bits */
#define PGD_SIZE      (1UL << 30)  /* 1 GiB：VPN[1] 9 bits + VPN[0] 9 bits + offset 12 bits */

/*
 * QEMU virt machine 上常見的 DRAM 與 UART 實體位址。
 *
 * PHYS_RAM_BASE 是 kernel 被載入與 RAM 線性映射開始的位置。
 * UART_PHYS_BASE 是 16550 UART 的 MMIO 實體位址。
 */
#define PHYS_RAM_BASE  0x80000000UL
#define UART_PHYS_BASE 0x10000000UL

/*
 * kernel 使用高位址直接存取 UART。
 *
 * setup_vm() 會建立 UART_VIRT_BASE -> UART_PHYS_BASE 的映射，
 * 因此 C 程式可以透過高位址區間碰 MMIO，而不是繼續依賴低位址。
 */
#define UART_VIRT_BASE (PAGE_OFFSET + UART_PHYS_BASE)

/*
 * VA bit-field shifts (Sv39)
 *
 * Sv39 每層頁表索引都是 9 bits，page offset 是 12 bits：
 *
 *   bit 38..30：PGD index，也就是 VPN[2]
 *   bit 29..21：PMD index，也就是 VPN[1]
 *   bit 20..12：PTE index，也就是 VPN[0]
 *   bit 11.. 0：page offset
 */
#define PGD_SHIFT     30
#define PMD_SHIFT     21
#define PTE_SHIFT     12

/* 每張 Sv39 頁表有 512 個 entry，因為每層索引都是 9 bits。 */
#define ENTRIES_PER_TABLE  512

/*
 * 取得特定位址在各層頁表中的索引。
 *
 * KERNEL_PGD_INDEX 是 PAGE_OFFSET 所在的 PGD entry。
 * PGD_INDEX(va) / PMD_INDEX(va) 則從虛擬位址中取出對應的 9-bit index。
 */
#define KERNEL_PGD_INDEX   ((PAGE_OFFSET >> PGD_SHIFT) & 0x1FF)
#define PGD_INDEX(va)      (((unsigned long)(va) >> PGD_SHIFT) & 0x1FF)
#define PMD_INDEX(va)      (((unsigned long)(va) >> PMD_SHIFT) & 0x1FF)

/*
 * 早期線性映射範圍與 PMD table 配置。
 *
 * LINEAR_MAP_GIB = 4 表示映射從 PHYS_RAM_BASE 開始的 4 GiB RAM 範圍。
 * 一個 PGD entry 對應 1 GiB，因此每 1 GiB 需要一張 PMD table。
 *
 * pmd[0] 保留給 UART MMIO 使用；
 * pmd[1] 之後才拿來映射 RAM，避免 UART 與 RAM 的 PMD table 混在一起。
 */
#define LINEAR_MAP_GIB     4
#define UART_PMD_TABLE     0
#define RAM_PMD_TABLE_BASE 1

/*
 * PTE descriptor bits (Sv39)
 *
 * V：valid，entry 是否有效
 * R/W/X：read/write/execute 權限
 * U：user page；這裡是 kernel mapping，所以不使用
 * G：global mapping，表示不同 address space 可共用這些 TLB entry
 * A：accessed，由硬體或軟體表示此頁曾被存取
 * D：dirty，由硬體或軟體表示此頁曾被寫入
 */
#define PTE_V  (1UL << 0)
#define PTE_R  (1UL << 1)
#define PTE_W  (1UL << 2)
#define PTE_X  (1UL << 3)
#define PTE_U  (1UL << 4)
#define PTE_G  (1UL << 5)
#define PTE_A  (1UL << 6)
#define PTE_D  (1UL << 7)

/*
 * kernel code/data 使用的權限。
 *
 * 這裡為了簡化早期映射，RAM 被標成 R/W/X。實際作業系統通常會
 * 進一步區分 text、rodata、data，避免所有 RAM 都可執行。
 */
#define PROT_KERNEL  (PTE_V | PTE_R | PTE_W | PTE_X | PTE_G | PTE_A | PTE_D)

/*
 * MMIO 不需要 execute 權限。
 *
 * UART 只需要 CPU 能讀寫裝置暫存器，因此 PROT_MMIO 沒有 PTE_X。
 */
#define PROT_MMIO    (PTE_V | PTE_R | PTE_W | PTE_G | PTE_A | PTE_D)

/*
 * satp 設定值。
 *
 * Sv39 模式在 satp[63:60] 填入 8。
 * satp 的 PPN 欄位放的是根頁表實體頁號，因此要把 pgd 位址右移 12 bits。
 */
#define SATP_SV39           (8UL << 60)
#define MAKE_SATP(pgd_pa)   (SATP_SV39 | ((unsigned long)(pgd_pa) >> 12))

/*
 * 將實體位址與 flag 組成 PTE。
 *
 * RISC-V PTE 的 PPN 從 bit 10 開始，低 10 bits 放權限與狀態位元。
 * 因此先把實體位址轉成 page number，再左移到 PTE 的 PPN 欄位。
 */
#define MAKE_PTE(pa, flags) ((((unsigned long)(pa)) >> 12) << 10 | (flags))

/*
 * PGD，也就是 Sv39 的根頁表。
 *
 * 必須 page aligned，因為 satp 只記錄 page number；如果頁表沒有
 * 4 KiB 對齊，硬體走頁表時會讀到錯誤位置。
 *
 * 放在 .data 而不是 .bss，是因為 start.S 在清 .bss 前就會呼叫
 * setup_vm()。若頁表放在 .bss，可能會被後面的 clear_bss 清掉。
 */
static unsigned long __attribute__((section(".data"), aligned(PAGE_SIZE)))
    pgd[ENTRIES_PER_TABLE] = { 0 };

/*
 * PMD tables。
 *
 * pmd[0] 給 UART MMIO。
 * pmd[1] 到 pmd[4] 給 4 GiB RAM 線性映射，每張 PMD table 管 1 GiB。
 */
static unsigned long __attribute__((section(".data"), aligned(PAGE_SIZE)))
    pmd[LINEAR_MAP_GIB + 1][ENTRIES_PER_TABLE] = { { 0 } };

/*
 * 建立早期 kernel 頁表並啟用 Sv39。
 *
 * 這個函式會建立兩種 RAM 映射：
 *
 *   1. identity mapping：
 *      VA == PA，例如 0x80000000 -> 0x80000000。
 *      用途是讓 CPU 在剛打開 MMU 後，仍能繼續執行當前低位址程式碼。
 *
 *   2. high-half / linear mapping：
 *      VA == PAGE_OFFSET + PA。
 *      用途是讓 kernel 之後固定從高位址區間存取 RAM。
 *
 * 同時也會映射 UART，讓 kernel 啟動後可以繼續使用 console 輸出。
 */
void setup_vm(void)
{
    /*
     * 每次迴圈處理 1 GiB RAM。
     *
     * 在 Sv39 中，一個 PGD entry 覆蓋 1 GiB；我們為每個 1 GiB 範圍
     * 準備一張 PMD table，並在 PMD 層用 2 MiB huge page 映射。
     */
    for (unsigned long gib = 0; gib < LINEAR_MAP_GIB; gib++) {
        /*
         * pa_base 是這個 1 GiB 區塊的實體起始位址。
         * pmd_table 是對應要填入的 pmd[][] 第幾張 table。
         */
        unsigned long pa_base = PHYS_RAM_BASE + gib * PGD_SIZE;
        unsigned long pmd_table = RAM_PMD_TABLE_BASE + gib;

        /*
         * 填滿這張 PMD table。
         *
         * 每個 PMD leaf entry 映射 2 MiB，因此 512 個 entry 正好映射
         * 1 GiB。這裡建立的是等距線性映射：第 i 格對應
         * pa_base + i * 2 MiB。
         */
        for (unsigned long i = 0; i < ENTRIES_PER_TABLE; i++) {
            unsigned long pa = pa_base + i * PMD_SIZE;
            pmd[pmd_table][i] = MAKE_PTE(pa, PROT_KERNEL);
        }

        /*
         * 建立 identity mapping 的 PGD entry。
         *
         * PGD entry 不是 leaf，指向剛才填好的 PMD table；因此 flag
         * 只需要 PTE_V，不放 R/W/X。
         */
        pgd[PGD_INDEX(pa_base)] = MAKE_PTE((unsigned long)pmd[pmd_table], PTE_V);

        /*
         * 建立 high-half / linear mapping 的 PGD entry。
         *
         * 這裡和 identity mapping 指向同一張 PMD table，所以同一批
         * PMD leaf entry 同時服務低位址與高位址兩種虛擬位址。
         */
        pgd[PGD_INDEX(PAGE_OFFSET + pa_base)] =
            MAKE_PTE((unsigned long)pmd[pmd_table], PTE_V);
    }

    /*
     * 建立 UART 的 MMIO 映射。
     *
     * UART_VIRT_BASE 位於 PAGE_OFFSET 高位址區間，但實際指到
     * UART_PHYS_BASE。PMD entry 使用 PROT_MMIO，所以可讀寫但不可執行。
     */
    pmd[UART_PMD_TABLE][PMD_INDEX(UART_VIRT_BASE)] =
        MAKE_PTE(UART_PHYS_BASE, PROT_MMIO);

    /*
     * 將 UART 專用 PMD table 掛到 PGD。
     *
     * 一樣地，這個 PGD entry 只是下一層頁表指標，因此只設 PTE_V。
     */
    pgd[PGD_INDEX(UART_VIRT_BASE)] =
        MAKE_PTE((unsigned long)pmd[UART_PMD_TABLE], PTE_V);

    /*
     * 啟用 Sv39 分頁。
     *
     * csrw satp, value：
     *   把根頁表 pgd 的 PPN 與 MODE=Sv39 寫入 satp。
     *
     * sfence.vma zero, zero：
     *   刷新所有位址空間、所有虛擬位址的 TLB，確保之後的記憶體存取
     *   都根據新頁表轉譯。
     *
     * memory clobber：
     *   告訴編譯器這段 asm 會影響記憶體觀察順序，避免它把頁表寫入
     *   重排到 satp 切換之後。
     */
    asm volatile(
        "csrw satp, %0\n"
        "sfence.vma zero, zero\n"
        :
        : "r"(MAKE_SATP((unsigned long)pgd))
        : "memory");
}

/*
 * 移除早期 identity mapping。
 *
 * setup_vm() 一開始同時建立低位址 identity mapping 與高位址 mapping，
 * 是為了讓 CPU 開 MMU 後還能平順跳到高位址。當 start.S 已經完成
 * 高位址跳轉後，identity mapping 就不再需要。
 */
void drop_identity_map(void)
{
    /*
     * 清掉每個 RAM 1 GiB 區塊在低位址的 PGD entry。
     *
     * 注意這裡只移除 VA == PA 的 mapping；PAGE_OFFSET + PA 的高位址
     * mapping 仍然保留，kernel 後續會靠它存取 RAM。
     */
    for (unsigned long gib = 0; gib < LINEAR_MAP_GIB; gib++)
        pgd[PGD_INDEX(PHYS_RAM_BASE + gib * PGD_SIZE)] = 0;

    /*
     * 頁表內容改變後必須刷新 TLB。
     *
     * 否則處理器可能繼續使用舊的 identity mapping 快取結果，
     * 讓已經清掉的低位址映射暫時仍然可用。
     */
    asm volatile("sfence.vma zero, zero" ::: "memory");
}

/*
 * C kernel 主入口。
 *
 * 目前這個練習的 kernel 行為很簡單：
 *   1. 透過 UART 印出 start_kernel() 目前所在位址。
 *   2. 進入 echo loop，讀到一個字元就原樣輸出。
 */
void start_kernel(void)
{
    /* 印出目前 kernel 已經在高位址 mapping 中執行的證據。 */
    uart_puts("\nStarting kernel at : ");
    uart_hex((unsigned long)start_kernel);
    uart_puts("\n");

    /*
     * UART echo loop。
     *
     * uart_getc() 會等待輸入一個字元；
     * uart_putc() 再把同一個字元送回 UART，形成簡單互動 console。
     */
    while (1) {
        uart_putc(uart_getc());
    }
}
