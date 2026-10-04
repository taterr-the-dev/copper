#include <kernel/acpi_bus.h>
#include <kernel/acpi.h>
#include <kernel/console.h>
#include <kernel/kmalloc.h>
#include <kernel/string.h>

extern void put_u64(uint64_t v);

static struct acpi_device *acpi_devices = NULL;
static struct acpi_driver *acpi_drivers = NULL;

int acpi_parse_tables(void) {
    con_puts("[ACPI] Parsing ACPI tables...\n");
    
    extern struct acpi_info acpi;
    for (uint32_t i = 0; i < acpi.cpu_count; i++) {
        struct acpi_device *cpu = kmalloc(sizeof(*cpu));
        if (!cpu) continue;
        
        memset(cpu, 0, sizeof(*cpu));
        
        cpu->name[0] = 'C';
        cpu->name[1] = 'P';
        cpu->name[2] = 'U';
        cpu->name[3] = '0' + i;
        cpu->name[4] = '\0';
        
        memcpy(cpu->hid, "ACPI0007", 9);
        
        cpu->uid[0] = '0' + acpi.cpu_apic_ids[i];
        cpu->uid[1] = '\0';
        
        cpu->status = 0xF;
        cpu->driver_data = NULL;
        cpu->next = acpi_devices;
        acpi_devices = cpu;
    }
    
    for (uint32_t i = 0; i < acpi.ioapic_count; i++) {
        struct acpi_device *ioapic = kmalloc(sizeof(*ioapic));
        if (!ioapic) continue;
        
        memset(ioapic, 0, sizeof(*ioapic));
        
        ioapic->name[0] = 'I';
        ioapic->name[1] = 'O';
        ioapic->name[2] = 'A';
        ioapic->name[3] = 'P';
        ioapic->name[4] = 'I';
        ioapic->name[5] = 'C';
        ioapic->name[6] = '0' + i;
        ioapic->name[7] = '\0';
        
        memcpy(ioapic->hid, "IOAPIC", 7);
        
        ioapic->uid[0] = '0' + (acpi.ioapic_gsi_bases[i] % 10);
        ioapic->uid[1] = '\0';
        
        ioapic->status = 0xF;
        ioapic->driver_data = NULL;
        ioapic->next = acpi_devices;
        acpi_devices = ioapic;
    }
    
    con_puts("[ACPI] Found ");
    put_u64(acpi.cpu_count + acpi.ioapic_count);
    con_puts(" device(s)\n");
    
    return 0;
}

int acpi_register_driver(struct acpi_driver *drv) {
    if (!drv) return -1;
    
    drv->next = acpi_drivers;
    acpi_drivers = drv;
    
    struct acpi_device *dev = acpi_devices;
    while (dev) {
        for (int i = 0; drv->match_hids[i]; i++) {
            if (strcmp(dev->hid, drv->match_hids[i]) == 0) {
                if (drv->probe) {
                    int ret = drv->probe(dev);
                    if (ret == 0) {
                        con_puts("[ACPI] Driver ");
                        con_puts(drv->name);
                        con_puts(" bound to ");
                        con_puts(dev->name);
                        con_puts("\n");
                    }
                }
                break;
            }
        }
        dev = dev->next;
    }
    
    return 0;
}

void acpi_unregister_driver(struct acpi_driver *drv) {
    if (!drv) return;
    
    struct acpi_driver **p = &acpi_drivers;
    while (*p) {
        if (*p == drv) {
            *p = drv->next;
            break;
        }
        p = &(*p)->next;
    }
    
    struct acpi_device *dev = acpi_devices;
    while (dev) {
        if (dev->driver_data && drv->remove) {
            drv->remove(dev);
            dev->driver_data = NULL;
        }
        dev = dev->next;
    }
}

struct acpi_device *acpi_find_device(const char *hid) {
    struct acpi_device *dev = acpi_devices;
    while (dev) {
        if (strcmp(dev->hid, hid) == 0) {
            return dev;
        }
        dev = dev->next;
    }
    return NULL;
}

int acpi_bus_init(void) {
    con_puts("[ACPI] Initializing ACPI bus...\n");
    int ret = acpi_parse_tables();
    if (ret != 0) {
        con_puts("[ACPI] Failed to parse tables\n");
        return ret;
    }
    
    con_puts("[ACPI] ACPI bus initialized\n");
    return 0;
}
