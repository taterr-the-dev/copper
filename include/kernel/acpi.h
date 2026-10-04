#ifndef _KERNEL_ACPI_H
#define _KERNEL_ACPI_H

#include <kernel/types.h>

struct acpi_sdt_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct acpi_rsdp {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

struct acpi_generic_address {
    uint8_t address_space_id;
    uint8_t register_bit_width;
    uint8_t register_bit_offset;
    uint8_t access_size;
    uint64_t address;
} __attribute__((packed));

struct acpi_madt {
    struct acpi_sdt_header header;
    uint32_t local_apic_address;
    uint32_t flags;
} __attribute__((packed));

struct acpi_madt_entry {
    uint8_t type;
    uint8_t length;
} __attribute__((packed));

struct acpi_madt_local_apic {
    uint8_t type;
    uint8_t length;
    uint8_t acpi_id;
    uint8_t apic_id;
    uint32_t flags;
} __attribute__((packed));

struct acpi_madt_io_apic {
    uint8_t type;
    uint8_t length;
    uint8_t io_apic_id;
    uint8_t reserved;
    uint32_t address;
    uint32_t gsi_base;
} __attribute__((packed));

struct acpi_madt_lapic_override {
    uint8_t type;
    uint8_t length;
    uint16_t reserved;
    uint64_t address;
} __attribute__((packed));

struct acpi_fadt {
    struct acpi_sdt_header header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t reserved0;
    uint8_t preferred_pm_profile;
    uint16_t sci_interrupt;
    uint32_t smi_command_port;
    uint8_t acpi_enable;
    uint8_t acpi_disable;
    uint8_t s4bios_req;
    uint8_t pstate_control;
    uint32_t pm1a_event_blk;
    uint32_t pm1b_event_blk;
    uint32_t pm1a_control_blk;
    uint32_t pm1b_control_blk;
    uint32_t pm2_control_blk;
    uint32_t pm_timer_blk;
    uint32_t gpe0_blk;
    uint32_t gpe1_blk;
    uint8_t pm1_event_length;
    uint8_t pm1_control_length;
    uint8_t pm2_control_length;
    uint8_t pm_timer_length;
    uint8_t gpe0_length;
    uint8_t gpe1_length;
    uint8_t gpe1_base;
    uint8_t cstate_control;
    uint16_t worst_c2_latency;
    uint16_t worst_c3_latency;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t duty_offset;
    uint8_t duty_width;
    uint8_t day_alarm;
    uint8_t month_alarm;
    uint8_t century;
    uint16_t boot_arch_flags;
    uint8_t reserved1;
    uint32_t flags;
} __attribute__((packed));

#define ACPI_MAX_CPUS 64
#define ACPI_MAX_IOAPICS 8

struct acpi_info {
    uint64_t lapic_address;
    int cpu_count;
    uint8_t cpu_apic_ids[ACPI_MAX_CPUS];
    int ioapic_count;
    uint32_t ioapic_addresses[ACPI_MAX_IOAPICS];
    uint32_t ioapic_gsi_bases[ACPI_MAX_IOAPICS];
    struct acpi_generic_address pm1a_cnt;
    struct acpi_generic_address pm1b_cnt;
    struct acpi_generic_address pm1a_evt;
    uint32_t dsdt_address;
    uint16_t sci_interrupt;
    int initialized;
};

extern struct acpi_info acpi;
extern struct acpi_rsdp *arch_acpi_get_rsdp(void);
extern void *arch_acpi_map_phys(uint64_t phys_addr, size_t size);
extern void arch_acpi_pm_write_gas(struct acpi_generic_address *gas, uint16_t val);
uint32_t arch_acpi_pm_read_gas(struct acpi_generic_address *gas);
void acpi_sci_handler(void);
void arch_acpi_unmask_sci(uint32_t irq);
extern void arch_acpi_shutdown_qemu(uint16_t val);

void acpi_init(void);
struct acpi_sdt_header *acpi_find_table(const char *signature);
void acpi_shutdown(void);

#endif
