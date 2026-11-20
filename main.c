#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <pspintrman.h>
#include <pspkdebug.h>
#include <psploadcore.h>
#include <pspsysmem_kernel.h>
#include <psputils.h>
#include <stddef.h>
#include <stdint.h>

PSP_MODULE_INFO("MagicGateTest", PSP_MODULE_KERNEL, 1, 0);

enum {
    FIRMWARE_661 = 0x06060110,
    PSP_MODEL_01G = 0,
    PSP_MODEL_02G = 1,
    PSP_MODEL_03G = 2,
    PSP_MODEL_04G = 3,
    PSP_MODEL_07G = 6,
    PSP_MODEL_09G = 8,
    DEVKIT_VERSION_NID_661 = 0xC886B169,
    TARGET_OFFSET = 0x7644,
    LAST_CHECK_OFFSET = 0x30
};

/* The bundled SDK's SceModule omits the reboot-phase entry present on 6.61. */
typedef struct {
    uint32_t next;
    uint16_t attribute;
    uint8_t version[2];
    char name[28];
    uint32_t unknown1;
    uint32_t unknown2;
    uint32_t modid;
    uint32_t unknown3[4];
    uint32_t ent_top;
    uint32_t ent_size;
    uint32_t stub_top;
    uint32_t stub_size;
    uint32_t module_entries[5];
    uint32_t entry_addr;
    uint32_t gp_value;
    uint32_t text_addr;
    uint32_t text_size;
    uint32_t data_size;
    uint32_t bss_size;
    uint8_t nsegment;
    uint8_t padding[3];
    uint32_t segmentaddr[4];
    uint32_t segmentsize[4];
} __attribute__((packed)) SceModule661;

_Static_assert(offsetof(SceModule661, ent_top) == 0x40, "incorrect export offset");
_Static_assert(offsetof(SceModule661, text_addr) == 0x6c, "incorrect text offset");
_Static_assert(offsetof(SceModule661, nsegment) == 0x7c, "incorrect segment offset");
_Static_assert(offsetof(SceModule661, segmentaddr) == 0x80, "incorrect segment addresses");

static int in_module_segment(const SceModule661 *module, uintptr_t address, unsigned int size)
{
    unsigned int i;

    if (module->nsegment > 4) {
        return 0;
    }

    for (i = 0; i < module->nsegment; i++) {
        uintptr_t start = module->segmentaddr[i];
        unsigned int length = module->segmentsize[i];

        if (address >= start && size <= length && address - start <= length - size) {
            return 1;
        }
    }

    return 0;
}

static unsigned int (*find_firmware_version(SceModule661 **found, const char **reason))(void)
{
    static const char library[] = "SysMemForKernel";
    SceModule661 *module = (SceModule661 *)(void *)
        sceKernelFindModuleByName("sceSystemMemoryManager");
    unsigned int offset = 0;

    *found = module;
    if (module == NULL) {
        *reason = "sysmem module missing";
        return NULL;
    }

    if (!in_module_segment(module, (uintptr_t)module->ent_top, module->ent_size)) {
        *reason = "export entries outside segments";
        return NULL;
    }

    *reason = "version export not found";
    while (offset <= module->ent_size && module->ent_size - offset >= sizeof(SceLibraryEntryTable)) {
        const SceLibraryEntryTable *entry =
            (const SceLibraryEntryTable *)((uintptr_t)module->ent_top + offset);
        unsigned int entry_size = entry->len * 4;
        unsigned int count = entry->stubcount + entry->vstubcount;
        unsigned int i;
        const unsigned int *table = entry->entrytable;

        if (entry_size < sizeof(SceLibraryEntryTable) || entry_size > module->ent_size - offset) {
            *reason = "invalid export entry size";
            return NULL;
        }

        if (entry->libname != NULL &&
            !in_module_segment(module, (uintptr_t)entry->libname, sizeof(library))) {
            *reason = "export name outside segments";
        }

        if (entry->libname != NULL &&
            in_module_segment(module, (uintptr_t)entry->libname, sizeof(library))) {
            for (i = 0; i < sizeof(library) && entry->libname[i] == library[i]; i++) {
            }

            if (i == sizeof(library) &&
                (table == NULL || !in_module_segment(module, (uintptr_t)table,
                                                       count * 2 * sizeof(*table)))) {
                *reason = "version table outside segments";
                return NULL;
            }

            if (i == sizeof(library)) {
                for (i = 0; i < entry->stubcount; i++) {
                    if (table[i] == DEVKIT_VERSION_NID_661) {
                        uintptr_t address = table[count + i];

                        if ((address & 3) == 0 && address >= module->text_addr &&
                            address - module->text_addr < module->text_size) {
                            return (unsigned int (*)(void))address;
                        }
                        *reason = "version function outside text";
                        return NULL;
                    }
                }
            }
        }

        offset += entry_size;
    }

    return NULL;
}

static void append_text(char *buffer, unsigned int *position, const char *text)
{
    while (*text != '\0') {
        buffer[(*position)++] = *text++;
    }
}

static void append_hex(char *buffer, unsigned int *position, unsigned int value)
{
    static const char digits[] = "0123456789ABCDEF";
    unsigned int i;

    append_text(buffer, position, "0x");
    for (i = 0; i < 8; i++) {
        buffer[(*position)++] = digits[(value >> (28 - i * 4)) & 0xF];
    }
}

static void report(const char *message)
{
    unsigned int length = 0;
    SceUID file;

    while (message[length] != '\0') {
        length++;
    }

    Kprintf("MagicGateTest: %s", message);
    file = sceIoOpen("ms0:/seplugins/magicgate-test.log",
                     PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (file < 0) {
        Kprintf("MagicGateTest: could not open log: 0x%08X\n", file);
        return;
    }

    if (sceIoWrite(file, message, length) != (int)length) {
        Kprintf("MagicGateTest: could not write complete log\n");
    }

    if (sceIoClose(file) < 0) {
        Kprintf("MagicGateTest: could not close log\n");
    }
}

static void report_lookup_failure(const SceModule661 *module, const char *reason)
{
    char message[320];
    unsigned int position = 0;

    append_text(message, &position, "Not patched: ");
    append_text(message, &position, reason);
    if (module != NULL) {
        append_text(message, &position, "\nnsegment=");
        append_hex(message, &position, module->nsegment);
        append_text(message, &position, " segment0=");
        append_hex(message, &position, module->segmentaddr[0]);
        append_text(message, &position, " size0=");
        append_hex(message, &position, module->segmentsize[0]);
        append_text(message, &position, "\nexports=");
        append_hex(message, &position, module->ent_top);
        append_text(message, &position, " export_size=");
        append_hex(message, &position, module->ent_size);
        append_text(message, &position, "\ntext=");
        append_hex(message, &position, module->text_addr);
        append_text(message, &position, " text_size=");
        append_hex(message, &position, module->text_size);
    }
    append_text(message, &position, "\nNo patch was applied.\n");
    message[position] = '\0';
    report(message);
}

int module_start(SceSize args, void *argp)
{
    SceModule661 *module;
    SceModule661 *sysmem;
    volatile unsigned int *target;
    unsigned int (*get_firmware_version)(void);
    const char *reason;
    unsigned int firmware;
    unsigned int interrupt_state;
    int model;
    int matches;

    (void)args;
    (void)argp;

    report("Patch test started; no patch installed yet.\n");

    get_firmware_version = find_firmware_version(&sysmem, &reason);
    if (get_firmware_version == NULL) {
        report_lookup_failure(sysmem, reason);
        return 1;
    }

    firmware = get_firmware_version();
    if (firmware != FIRMWARE_661) {
        static const char digits[] = "0123456789ABCDEF";
        char message[] = "Not patched: reported firmware 0x00000000 (expected 0x06060110).\n";
        unsigned int i;

        for (i = 0; i < 8; i++) {
            message[sizeof("Not patched: reported firmware 0x") - 1 + i] =
                digits[(firmware >> (28 - i * 4)) & 0xF];
        }

        report(message);
        return 1;
    }

    model = sceKernelGetModel();
    if (model != PSP_MODEL_01G && model != PSP_MODEL_02G &&
        model != PSP_MODEL_03G && model != PSP_MODEL_04G &&
        model != PSP_MODEL_07G && model != PSP_MODEL_09G) {
        report("Not patched: this model is not a supported PSP-1000, 2000, or 3000.\n");
        return 1;
    }

    module = (SceModule661 *)(void *)sceKernelFindModuleByName("sceMgr_Driver");
    if (module == NULL) {
        report("Not patched: sceMgr_Driver was not loaded.\n");
        return 1;
    }

    if (module->text_size < TARGET_OFFSET + LAST_CHECK_OFFSET + sizeof(unsigned int)) {
        report("Not patched: sceMgr_Driver text is too small.\n");
        return 1;
    }

    target = (volatile unsigned int *)((uintptr_t)module->text_addr + TARGET_OFFSET);
    interrupt_state = sceKernelCpuSuspendIntr();
    matches = target[0] == 0x27bdffd0 &&
              target[1] == 0xafb60028 &&
              target[2] == 0x0080b021 &&
              target[3] == 0xafb3001c &&
              target[7] == 0xafb00010 &&
              target[8] == 0xafbf002c &&
              target[9] == 0xafb50024 &&
              target[10] == 0xafb40020 &&
              target[12] == 0xafb10014;

    if (matches) {
        target[0] = 0x3c028050;
        target[1] = 0x24424002;
        target[2] = 0x03e00008;
        target[3] = 0x00000000;
        sceKernelDcacheWritebackInvalidateAll();
        sceKernelIcacheInvalidateAll();
    }

    sceKernelCpuResumeIntr(interrupt_state);

    if (!matches) {
        report("Not patched: sceMgr_Driver instructions did not match.\n");
        return 1;
    }

    report("Patch installed in RAM. Disable the plugin and reboot to remove it.\n");
    return 0;
}
