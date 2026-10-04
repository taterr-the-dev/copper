#include <kernel/acpi_bus.h>
#include <kernel/console.h>

static int cpu_probe(struct acpi_device *dev) {
    con_puts("[CPU] Probing ");
    con_puts(dev->name);
    con_puts(" (HID: ");
    con_puts(dev->hid);
    con_puts(")\n");
    
    dev->driver_data = (void *)0x1;
    
    return 0;
}

static int cpu_remove(struct acpi_device *dev) {
    con_puts("[CPU] Removing ");
    con_puts(dev->name);
    con_puts("\n");
    dev->driver_data = NULL;
    return 0;
}

static struct acpi_driver cpu_driver = {
    .name = "acpi-cpu",
    .match_hids = {"ACPI0007", NULL},
    .probe = cpu_probe,
    .remove = cpu_remove,
};

void acpi_cpu_init(void) {
    acpi_register_driver(&cpu_driver);
}
