#ifndef _KERNEL_ACPI_BUS_H
#define _KERNEL_ACPI_BUS_H

#include <kernel/types.h>

struct acpi_device {
    char name[32];
    char hid[16];
    char uid[16];
    uint64_t status;
    void *driver_data;
    struct acpi_device *next;
};

struct acpi_driver {
    char name[32];
    const char *match_hids[8];
    int (*probe)(struct acpi_device *dev);
    int (*remove)(struct acpi_device *dev);
    struct acpi_driver *next;
};

int acpi_bus_init(void);
int acpi_register_driver(struct acpi_driver *drv);
void acpi_unregister_driver(struct acpi_driver *drv);
struct acpi_device *acpi_find_device(const char *hid);

int acpi_parse_tables(void);
#endif
