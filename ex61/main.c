extern char uart_getc(void);
extern void uart_putc(char c);
extern void uart_puts(const char* s);
extern void uart_hex(unsigned long h);

/* Memory map */
#define PAGE_OFFSET   0xffffffc000000000UL
#define PAGE_SIZE     (1UL << 12) 
#define PMD_SIZE      (1UL << 21)
#define PGD_SIZE      (1UL << 30)

/* VA bit-field shifts (Sv39) */
#define PGD_SHIFT     30 
#define PMD_SHIFT     21
#define PTE_SHIFT     12

#define ENTRIES_PER_TABLE  512

#define KERNEL_PGD_INDEX   ((PAGE_OFFSET >> PGD_SHIFT) & 0x1FF)

#define LINEAR_MAP_GIB     4

/* PTE descriptor bits (Sv39) */
#define PTE_V  (1UL << 0)  
#define PTE_R  (1UL << 1)
#define PTE_W  (1UL << 2)
#define PTE_X  (1UL << 3)
#define PTE_U  (1UL << 4)
#define PTE_G  (1UL << 5)
#define PTE_A  (1UL << 6)
#define PTE_D  (1UL << 7)

#define PROT_KERNEL  (PTE_V | PTE_R | PTE_W | PTE_X | PTE_G | PTE_A | PTE_D)

#define SATP_SV39           (8UL << 60)
#define MAKE_SATP(pgd_pa)   (SATP_SV39 | ((unsigned long)(pgd_pa) >> 12))

#define MAKE_PTE(pa, flags) ((((unsigned long)(pa)) >> 12) << 10 | (flags))


static unsigned long __attribute__((section(".data"), aligned(PAGE_SIZE)))
    pgd[ENTRIES_PER_TABLE] = { 0 };

static unsigned long __attribute__((section(".data"), aligned(PAGE_SIZE)))
    pmd_kernel[LINEAR_MAP_GIB][ENTRIES_PER_TABLE] = { { 0 } };

static unsigned long __attribute__((section(".data"), aligned(PAGE_SIZE)))
    pmd_identity[LINEAR_MAP_GIB][ENTRIES_PER_TABLE] = { { 0 } };

void setup_vm(void)
{
    // TODO: Set up page tables for identity mapping and kernel mapping
}

void drop_identity_map(void)
{
    // TODO: Drop identity mapping
}

void start_kernel(void)
{
    uart_puts("\nStarting kernel at : ");
    uart_hex((unsigned long)start_kernel);
    uart_puts("\n");
    while (1) {
        uart_putc(uart_getc());
    }
}