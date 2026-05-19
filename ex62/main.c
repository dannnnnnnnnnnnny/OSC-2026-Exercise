extern char uart_getc(void);
extern void uart_putc(char c);
extern void uart_puts(const char* s);
extern void uart_hex(unsigned long h);
extern int hextoi(const char* s, int n);
extern int align(int n, int byte);
extern int memcmp(const void* s1, const void* s2, int n);
extern void* memcpy(void* dst, const void* src, int n);
extern void* memset(void* s, int c, int n);
extern void* alloc_page();

/*
 * ex62 的主題是「細粒度 4KB page mapping」與「切換到 RISC-V U-mode」。
 *
 * 整體流程如下：
 * 1. setup_vm() 先建立 kernel high-half direct mapping，並啟用 Sv39。
 * 2. exec() 從 initramfs 裡找到 prog.bin，配置一頁記憶體保存 user code。
 * 3. pagewalk() 依照 Sv39 三層頁表格式，動態建立缺少的中間頁表，
 *    並把 user code 與 user stack 映射到 user 虛擬位址空間。
 * 4. exec() 設定 sepc、sscratch、sstatus 後執行 sret，CPU 就會從
 *    S-mode 降到 U-mode，開始執行 user program。
 * 5. user program 透過 ecall 進入 trap，handle_exception 保存暫存器，
 *    再呼叫 do_trap() 印出 sepc/scause。
 */

/* 目前以 4KB page 為單位，映射 0x280000 個 page，也就是 10GB。 */
#define NUM_PAGES 0x280000

/*
 * Kernel high-half direct mapping 的基準位址。
 *
 * link.ld 將 kernel 連結到 PAGE_OFFSET + 0x80200000。啟用 MMU 後，
 * kernel 以高位址執行；若要把 kernel 配置到的頁面寫入 PTE，就必須先
 * 用 virt_to_phys() 轉回實體位址。
 */
#define PAGE_OFFSET 0xffffffc000000000UL

/* Sv39 使用 4KB page；PGD leaf entry 則可以直接映射 1GB huge page。 */
#define PAGE_SIZE   (1UL << 12)
#define PGD_SIZE    (1UL << 30)

/* 將位址轉成 page frame number，也就是 RISC-V PTE 裡的 PPN 來源。 */
#define PFN_DOWN(x) ((x) >> 12)

/*
 * RISC-V Sv39 PTE descriptor bits。
 *
 * V：valid，表示這個 PTE 可以被硬體 page-table walker 使用。
 * R/W/X：讀、寫、執行權限；中間層 page-table pointer 不能設這些 bit。
 * U：user page；U-mode 要能存取的頁面必須設 PTE_U。
 * G：global mapping；kernel direct mapping 可使用。
 * A/D：accessed / dirty。這份練習直接預先設好，避免因 A/D bit 缺失
 *      而產生額外 page fault。
 */
#define PTE_V  (1UL << 0)
#define PTE_R  (1UL << 1)
#define PTE_W  (1UL << 2)
#define PTE_X  (1UL << 3)
#define PTE_U  (1UL << 4)
#define PTE_G  (1UL << 5)
#define PTE_A  (1UL << 6)
#define PTE_D  (1UL << 7)
#define PTE_SOFT (3UL << 8)

/* Kernel mapping 為了簡化 early boot，直接給 R/W/X/G/A/D。 */
#define PROT_KERNEL    (PTE_V | PTE_R | PTE_W | PTE_X | PTE_G | PTE_A | PTE_D)

/* User mapping 的共同權限：valid、user-accessible，並預設 A/D。 */
#define PROT_USER_BASE (PTE_V | PTE_U | PTE_A | PTE_D)

/* User text/code：可讀、可執行、不可寫。 */
#define PROT_USER_RX   (PROT_USER_BASE | PTE_R | PTE_X)

/* User stack/data：可讀、可寫、不可執行。 */
#define PROT_USER_RW   (PROT_USER_BASE | PTE_R | PTE_W)

/* satp.MODE = 8 代表 Sv39；satp 低位元存 root page table 的 PPN。 */
#define SATP_SV39 (8UL << 60)

/* high-half direct map 下的簡單 VA/PA 轉換。 */
#define virt_to_phys(x) ((unsigned long)(x) - PAGE_OFFSET)
#define phys_to_virt(x) ((unsigned long)(x) + PAGE_OFFSET)

/*
 * Sv39 root page table，也就是 level-2 page table / PGD。
 *
 * 必須 4KB 對齊，因為 satp 只記錄 root table 的 PPN。若 pgd 沒有 page
 * aligned，硬體會從錯誤的實體頁開始解讀 PTE。
 *
 * 放在 .data 而不是 .bss，是因為 start.S 在清 .bss 前就會呼叫 setup_vm()。
 * 如果放在 .bss，已建立的 early mapping 會被後面的 clear_bss 清掉。
 */
unsigned long __attribute__((section(".data"), aligned(PAGE_SIZE))) pgd[512];

/*
 * 建立 kernel early mapping 並啟用 Sv39。
 *
 * 這裡使用 PGD leaf entry 直接映射 1GB huge page：
 *   VA = PAGE_OFFSET + PA
 *
 * PAGE_OFFSET 在 Sv39 的 root index 是 256，因此 pgd[256 + i] 對應第 i 個
 * 1GB 實體區塊。這讓 kernel 可以用 high-half VA 存取 RAM、kernel image、
 * alloc_page() 配出的頁面，以及 QEMU 載入的 initramfs。
 */
void setup_vm() {
    for (int i = 0; i < NUM_PAGES / (PGD_SIZE / PAGE_SIZE); i++) {
        pgd[256 + i] = (i * (PGD_SIZE / PAGE_SIZE)) << 10 | PROT_KERNEL;
    }

    /* 寫入 satp 後刷新 TLB，確保後續取指與讀寫都使用新的頁表。 */
    asm("csrw satp, %0" ::"r"(PFN_DOWN((unsigned long)pgd) |
                              SATP_SV39));
    asm("sfence.vma");
}

/*
 * 建立單一 4KB page mapping。
 *
 * Sv39 虛擬位址格式：
 *   bit 38..30：VPN[2]，索引 root / level-2 table，也就是 pgd。
 *   bit 29..21：VPN[1]，索引 level-1 table。
 *   bit 20..12：VPN[0]，索引 level-0 table。
 *   bit 11.. 0：page offset，不會寫進 PTE。
 *
 * pagewalk() 會從 level 2 往 level 0 走。若中間層 PTE 不存在，就配置一頁
 * 4KB 記憶體當下一層 page table，清零後把它掛到目前 PTE。
 */
static void pagewalk(unsigned long va, unsigned long pa, unsigned long prot) {
    unsigned long* table = pgd;

    /* 先拆出每一層的 9-bit VPN index。 */
    unsigned long vpn[3] = {
        (va >> 12) & 0x1ff,
        (va >> 21) & 0x1ff,
        (va >> 30) & 0x1ff,
    };

    for (int level = 2; level > 0; level--) {
        unsigned long* pte = &table[vpn[level]];

        /*
         * 中間層 PTE 只設 PTE_V，不設 R/W/X。
         *
         * alloc_page() 回傳 kernel high-half VA；PTE 裡需要的是實體 PPN，
         * 所以先 virt_to_phys()，再右移成 PFN，最後左移到 PTE.PPN 欄位。
         */
        if (!(*pte & PTE_V)) {
            unsigned long* next = alloc_page();
            memset(next, 0, PAGE_SIZE);
            *pte = (PFN_DOWN(virt_to_phys(next)) << 10) | PTE_V;
        }

        /* 目前 PTE 指向下一層 page table：取出 PPN 還原成 PA，再轉成 VA。 */
        table = (unsigned long*)phys_to_virt(((*pte >> 10) << 12));
    }

    /* level-0 PTE 是真正映射 4KB page 的 leaf entry。 */
    table[vpn[0]] = (PFN_DOWN(pa) << 10) | prot;
}

/* 把一段連續 VA/PA 範圍切成 4KB page，逐頁建立映射。 */
void map_pages(unsigned long va,
               unsigned long size,
               unsigned long pa,
               unsigned long prot) {
    for (int i = 0; i < size; i += PAGE_SIZE)
        pagewalk(va + i, pa + i, prot);
}

/*
 * QEMU virt machine 透過 -initrd 載入的 cpio archive 起始實體位址。
 *
 * 這個環境產生的 device tree 中 linux,initrd-start 是 0x88200000。ex62
 * 目前沒有解析 FDT，因此先用固定值，再透過 high-half direct map 存取。
 */
#define INITRD_BASE phys_to_virt(0x88200000)

/* newc cpio header；除了 magic/name/data 之外，數值欄位都是 ASCII hex。 */
struct cpio_t {
    char magic[6];
    char ino[8];
    char mode[8];
    char uid[8];
    char gid[8];
    char nlink[8];
    char mtime[8];
    char filesize[8];
    char devmajor[8];
    char devminor[8];
    char rdevmajor[8];
    char rdevminor[8];
    char namesize[8];
    char check[8];
};

/*
 * 從 initramfs 載入 user program，建立 user mapping，並透過 sret 進入 U-mode。
 *
 * 這份 initramfs 只有一個測試程式 prog.bin。它的內容很小，可以直接放進
 * 一個 alloc_page() 配出的頁面。user VA 0x0 映射到該 program page，
 * user stack 則映射在 0x4000000000 下方一頁。
 */
int exec(const char* filename) {
    char* p = (char*)INITRD_BASE;
    while (memcmp(p + sizeof(struct cpio_t), "TRAILER!!!", 10)) {
        struct cpio_t* hdr = (struct cpio_t*)p;

        /* cpio newc 的大小欄位是 8 字元 ASCII hex，必須先轉成整數。 */
        int namesize = hextoi(hdr->namesize, 8);
        int filesize = hextoi(hdr->filesize, 8);

        /* header+filename 與 file data 都以 4 bytes 對齊。 */
        int headsize = align(sizeof(struct cpio_t) + namesize, 4);
        int datasize = align(filesize, 4);

        if (!memcmp(p + sizeof(struct cpio_t), filename, namesize)) {
            void* program = alloc_page();  // 測試程式很小，一頁就放得下。
            memcpy(program, p + headsize, filesize);

            /* user code 從 VA 0 開始執行，權限是 user read/execute。 */
            map_pages(0x0, filesize, virt_to_phys(program), PROT_USER_RX);

            /* 初始 user sp 是 0x4000000000，因此映射它下方的一頁當 stack。 */
            map_pages(0x3ffffff000, PAGE_SIZE, virt_to_phys(alloc_page()),
                      PROT_USER_RW);

            /* 建好 user mapping 後刷新 TLB，避免 sret 後看到舊 translation。 */
            asm volatile("sfence.vma" ::: "memory");

            /* sepc 是 sret 返回後的 PC；設為 0 代表從 user VA 0 開始跑。 */
            asm volatile("csrw sepc, %0" : : "r"(0x0));

            /* sscratch 保存 kernel sp，trap 進來時 start.S 會用它切回 kernel stack。 */
            asm volatile("csrw sscratch, sp");

            /* 切換到 user stack top；實際映射的是 top 下方那一頁。 */
            asm volatile("mv sp, %0" ::"r"(0x4000000000));

            /* 清掉 sstatus.SPP，讓 sret 返回到 U-mode，而不是 S-mode。 */
            asm volatile(
                "li t0, (1 << 8);"
                "csrc sstatus, t0;");
            asm volatile("sret");
        }
        p += headsize + datasize;
    }
    return -1;
}

/* Kernel C 入口：印啟動訊息、執行 prog.bin；若失敗才回到 UART echo loop。 */
void start_kernel() {
    uart_puts("\nStarting kernel ...\n");
    if (exec("prog.bin"))
        uart_puts("Failed to exec user program!\n");
    while (1) {
        uart_putc(uart_getc());
    }
}

/*
 * handle_exception 會依照這個順序把暫存器保存到 kernel stack，然後把 sp
 * 當作 struct pt_regs* 傳給 do_trap()。
 *
 * 因此這個 struct 的欄位順序必須和 start.S 裡的 sd/ld offset 完全一致。
 */
struct pt_regs {
    unsigned long ra;
    unsigned long sp;
    unsigned long gp;
    unsigned long tp;
    unsigned long t0;
    unsigned long t1;
    unsigned long t2;
    unsigned long s0;
    unsigned long s1;
    unsigned long a0;
    unsigned long a1;
    unsigned long a2;
    unsigned long a3;
    unsigned long a4;
    unsigned long a5;
    unsigned long a6;
    unsigned long a7;
    unsigned long s2;
    unsigned long s3;
    unsigned long s4;
    unsigned long s5;
    unsigned long s6;
    unsigned long s7;
    unsigned long s8;
    unsigned long s9;
    unsigned long s10;
    unsigned long s11;
    unsigned long t3;
    unsigned long t4;
    unsigned long t5;
    unsigned long t6;
    unsigned long epc;
    unsigned long status;
    unsigned long cause;
    unsigned long badaddr;
};

/*
 * C 端 trap handler。
 *
 * 測試程式會重複執行 ecall。scause=8 表示 Environment call from U-mode。
 * user program 第一條指令是 16-bit compressed instruction，ecall 位於 VA 0x2；
 * 處理完後把 epc 加 4，跳過 ecall 指令，讓 sret 回 user 後能繼續跑。
 */
void do_trap(struct pt_regs* regs) {
    uart_puts("sepc: ");
    uart_hex(regs->epc);
    uart_puts(", scause: ");
    uart_hex(regs->cause);
    uart_puts("\n");
    regs->epc += 4;
}
