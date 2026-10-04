#include <kernel/acpi.h>
#include <kernel/console.h>
#include <kernel/string.h>
#include <kernel/arch.h>
#include <kernel/interrupts.h>

extern void put_u64(uint64_t v);
struct acpi_info acpi;

static struct acpi_sdt_header *sdt_entries[64];
static int sdt_entry_count = 0;

static int acpi_validate_checksum(void *ptr, size_t len) {
    uint8_t sum = 0;
    uint8_t *p = (uint8_t *)ptr;
    for (size_t i = 0; i < len; i++) sum += p[i];
    return (sum == 0);
}

static void acpi_parse_madt(struct acpi_sdt_header *hdr) {
    struct acpi_madt *madt = (struct acpi_madt *)hdr;
    acpi.lapic_address = madt->local_apic_address;

    uint8_t *ptr = (uint8_t *)madt + sizeof(struct acpi_madt);
    uint8_t *end = (uint8_t *)madt + madt->header.length;

    while (ptr < end) {
        struct acpi_madt_entry *entry = (struct acpi_madt_entry *)ptr;

        if (entry->type == 0 && entry->length == 8) {
            struct acpi_madt_local_apic *lapic = (struct acpi_madt_local_apic *)ptr;
            if (lapic->flags & 1) {
                if (acpi.cpu_count < ACPI_MAX_CPUS) {
                    acpi.cpu_apic_ids[acpi.cpu_count] = lapic->apic_id;
                    acpi.cpu_count++;
                }
            }
        } else if (entry->type == 1 && entry->length == 12) {
            struct acpi_madt_io_apic *ioapic = (struct acpi_madt_io_apic *)ptr;
            if (acpi.ioapic_count < ACPI_MAX_IOAPICS) {
                acpi.ioapic_addresses[acpi.ioapic_count] = ioapic->address;
                acpi.ioapic_gsi_bases[acpi.ioapic_count] = ioapic->gsi_base;
                acpi.ioapic_count++;
            }
        } else if (entry->type == 5 && entry->length == 12) {
            struct acpi_madt_lapic_override *ovr = (struct acpi_madt_lapic_override *)ptr;
            acpi.lapic_address = ovr->address;
        }

        ptr += entry->length;
        if (entry->length == 0) break;
    }
}

static void acpi_parse_fadt(struct acpi_sdt_header *hdr) {
    struct acpi_fadt *fadt = (struct acpi_fadt *)hdr;
    acpi.dsdt_address = fadt->dsdt;
    acpi.sci_interrupt = fadt->sci_interrupt;

    if (hdr->revision >= 2 && hdr->length >= 208) {
        uint8_t *fadt_bytes = (uint8_t *)fadt;
        struct acpi_generic_address *x_pm1a = (struct acpi_generic_address *)(fadt_bytes + 184);
        acpi.pm1a_cnt = *x_pm1a;
        struct acpi_generic_address *x_pm1b = (struct acpi_generic_address *)(fadt_bytes + 196);
        acpi.pm1b_cnt = *x_pm1b;
    } else {
        acpi.pm1a_cnt.address_space_id = 1;
        acpi.pm1a_cnt.register_bit_width = 16;
        acpi.pm1a_cnt.address = fadt->pm1a_control_blk;
        
        acpi.pm1b_cnt.address_space_id = 1;
        acpi.pm1b_cnt.register_bit_width = 16;
        acpi.pm1b_cnt.address = fadt->pm1b_control_blk;
    }
    if (hdr->revision >= 2 && hdr->length >= 140) {
        uint8_t *fadt_bytes = (uint8_t *)fadt;
        struct acpi_generic_address *x_pm1a_evt = (struct acpi_generic_address *)(fadt_bytes + 112);
        acpi.pm1a_evt = *x_pm1a_evt;
    } else {
        acpi.pm1a_evt.address_space_id = 1;
        acpi.pm1a_evt.register_bit_width = 32;
        acpi.pm1a_evt.address = fadt->pm1a_event_blk;
    }
}

void acpi_sci_handler(void) {
    if (!acpi.initialized || acpi.pm1a_evt.address == 0) return;
    uint32_t sts = arch_acpi_pm_read_gas(&acpi.pm1a_evt);
    if (sts & 0x100) {
        arch_acpi_pm_write_gas(&acpi.pm1a_evt, 0x100);
        con_puts("\n[ACPI] Power button pressed! Shutting down cleanly...\n");
        acpi_shutdown();
    }
}

void acpi_enable_power_button(void) {
    if (!acpi.initialized || acpi.pm1a_evt.address == 0) return;
    struct acpi_generic_address en_reg = acpi.pm1a_evt;
    en_reg.address += 2;
    arch_acpi_pm_write_gas(&en_reg, 0x100);
    pic_clear_mask(acpi.sci_interrupt);
    con_puts("[ACPI] Power button event enabled.");
}

void acpi_init(void) {
    memset(&acpi, 0, sizeof(acpi));

    struct acpi_rsdp *rsdp = arch_acpi_get_rsdp();
    if (!rsdp || !acpi_validate_checksum(rsdp, 20)) {
        con_puts("[ACPI] RSDP not found or invalid\n");
        return;
    }

    void *sdt_phys = NULL;
    int is_xsdt = 0;
    if (rsdp->revision >= 2 && rsdp->xsdt_address != 0) {
        sdt_phys = (void *)rsdp->xsdt_address;
        is_xsdt = 1;
    } else {
        sdt_phys = (void *)(uint64_t)rsdp->rsdt_address;
    }

    struct acpi_sdt_header *sdt = (struct acpi_sdt_header *)arch_acpi_map_phys((uint64_t)sdt_phys, 4096);
    if (!sdt || !acpi_validate_checksum(sdt, sdt->length)) {
        con_puts("[ACPI] SDT invalid\n");
        return;
    }

    uint8_t *entries_start = (uint8_t *)sdt + sizeof(struct acpi_sdt_header);
    size_t entries_len = sdt->length - sizeof(struct acpi_sdt_header);

    if (is_xsdt) {
        uint64_t *ptrs = (uint64_t *)entries_start;
        sdt_entry_count = entries_len / sizeof(uint64_t);
        for (int i = 0; i < sdt_entry_count && i < 64; i++)
            sdt_entries[i] = (struct acpi_sdt_header *)arch_acpi_map_phys(ptrs[i], 4096);
    } else {
        uint32_t *ptrs = (uint32_t *)entries_start;
        sdt_entry_count = entries_len / sizeof(uint32_t);
        for (int i = 0; i < sdt_entry_count && i < 64; i++)
            sdt_entries[i] = (struct acpi_sdt_header *)arch_acpi_map_phys((uint64_t)ptrs[i], 4096);
    }

    for (int i = 0; i < sdt_entry_count; i++) {
        if (!sdt_entries[i]) continue;
        char *sig = sdt_entries[i]->signature;
        if (sig[0] == 'A' && sig[1] == 'P' && sig[2] == 'I' && sig[3] == 'C')
            acpi_parse_madt(sdt_entries[i]);
        if (sig[0] == 'F' && sig[1] == 'A' && sig[2] == 'C' && sig[3] == 'P')
            acpi_parse_fadt(sdt_entries[i]);
    }
    con_puts("[ACPI] ");
    put_u64(acpi.cpu_count);
    con_puts(" CPU(s), ");
    put_u64(acpi.ioapic_count);
    con_puts(" I/O APIC(s)\n");
    acpi.initialized = 1;
		acpi_enable_power_button();
}

struct acpi_sdt_header *acpi_find_table(const char *signature) {
    if (!acpi.initialized) return NULL;
    for (int i = 0; i < sdt_entry_count; i++) {
        if (sdt_entries[i] &&
            sdt_entries[i]->signature[0] == signature[0] &&
            sdt_entries[i]->signature[1] == signature[1] &&
            sdt_entries[i]->signature[2] == signature[2] &&
            sdt_entries[i]->signature[3] == signature[3])
            return sdt_entries[i];
    }
    return NULL;
}

void acpi_shutdown(void) {
    if (!acpi.initialized) {
        con_puts("[ACPI] Cannot shutdown: ACPI not initialized.\n");
        return;
    }
    
    uint16_t s5_cmd = 0x3400;

    if (acpi.pm1a_cnt.address != 0) {
        arch_acpi_pm_write_gas(&acpi.pm1a_cnt, s5_cmd);
    }
    
    if (acpi.pm1b_cnt.address != 0) {
        arch_acpi_pm_write_gas(&acpi.pm1b_cnt, s5_cmd);
    }

    arch_acpi_shutdown_qemu(s5_cmd);

    arch_cli();
    for (;;) arch_hlt();
}
