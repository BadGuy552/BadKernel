/*
 * Greybus "Core"
 *
 * Copyright 2014-2015 Google Inc.
 * Copyright 2014-2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define CREATE_TRACE_POINTS
#include "greybus.h"
#include "greybus_trace.h"

EXPORT_TRACEPOINT_SYMBOL_GPL(gb_host_device_send);
EXPORT_TRACEPOINT_SYMBOL_GPL(gb_host_device_recv);

/* Allow greybus to be disabled at boot if needed */
static bool nogreybus;
#ifdef MODULE
module_param(nogreybus, bool, 0444);
#else
core_param(nogreybus, nogreybus, bool, 0444);
#endif
int greybus_disabled(void)
{
	return nogreybus;
}
EXPORT_SYMBOL_GPL(greybus_disabled);

static int greybus_module_match(struct device *dev, struct device_driver *drv)
{
	struct greybus_driver *driver = to_greybus_driver(drv);
	struct gb_bundle *bundle = to_gb_bundle(dev);
	const struct greybus_bundle_id *id;

	id = gb_bundle_match_id(bundle, driver->id_table);
	if (id)
		return 1;
	/* FIXME - Dynamic ids? */
	return 0;
}

static int greybus_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct gb_module *module = NULL;
	struct gb_interface *intf = NULL;
	struct gb_bundle *bundle = NULL;

	if (is_gb_endo(dev)) {
		/*
		 * Not much to do for an endo, just fall through, as the
		 * "default" attributes are good enough for us.
		 */
		return 0;
	}

	if (is_gb_module(dev)) {
		module = to_gb_module(dev);
	} else if (is_gb_interface(dev)) {
		intf = to_gb_interface(dev);
	} else if (is_gb_bundle(dev)) {
		bundle = to_gb_bundle(dev);
		intf = bundle->intf;
	} else {
		dev_WARN(dev, "uevent for unknown greybus device \"type\"!\n");
		return -EINVAL;
	}

	if (bundle) {
		// FIXME
		// add a uevent that can "load" a bundle type
		// This is what we need to bind a driver to so use the info
		// in gmod here as well
		return 0;
	}

	// FIXME
	// "just" a module, be vague here, nothing binds to a module except
	// the greybus core, so there's not much, if anything, we need to
	// advertise.
	return 0;
}

struct bus_type greybus_bus_type = {
	.name =		"greybus",
	.match =	greybus_module_match,
	.uevent =	greybus_uevent,
};

static int greybus_probe(struct device *dev)
{
	struct greybus_driver *driver = to_greybus_driver(dev->driver);
	struct gb_bundle *bundle = to_gb_bundle(dev);
	const struct greybus_bundle_id *id;
	int retval;

	/* match id */
	id = gb_bundle_match_id(bundle, driver->id_table);
	if (!id)
		return -ENODEV;

	retval = driver->probe(bundle, id);
	if (retval)
		return retval;

	return 0;
}

static int greybus_remove(struct device *dev)
{
	struct greybus_driver *driver = to_greybus_driver(dev->driver);
	struct gb_bundle *bundle = to_gb_bundle(dev);

	driver->disconnect(bundle);
	return 0;
}

int greybus_register_driver(struct greybus_driver *driver, struct module *owner,
		const char *mod_name)
{
	int retval;

	if (greybus_disabled())
		return -ENODEV;

	driver->driver.name = driver->name;
	driver->driver.probe = greybus_probe;
	driver->driver.remove = greybus_remove;
	driver->driver.owner = owner;
	driver->driver.mod_name = mod_name;

	retval = driver_register(&driver->driver);
	if (retval)
		return retval;

	pr_info("registered new driver %s\n", driver->name);
	return 0;
}
EXPORT_SYMBOL_GPL(greybus_register_driver);

void greybus_deregister_driver(struct greybus_driver *driver)
{
	driver_unregister(&driver->driver);
}
EXPORT_SYMBOL_GPL(greybus_deregister_driver);

static int __init gb_init(void)
{
	int retval;

	if (greybus_disabled())
		return -ENODEV;

	BUILD_BUG_ON(CPORT_ID_MAX >= (long)CPORT_ID_BAD);

	gb_debugfs_init();

	retval = bus_register(&greybus_bus_type);
	if (retval) {
		pr_err("bus_register failed (%d)\n", retval);
		goto error_bus;
	}

	retval = gb_operation_init();
	if (retval) {
		pr_err("gb_operation_init failed (%d)\n", retval);
		goto error_operation;
	}

	retval = gb_endo_init();
	if (retval) {
		pr_err("gb_endo_init failed (%d)\n", retval);
		goto error_endo;
	}

	retval = gb_control_protocol_init();
	if (retval) {
		pr_err("gb_control_protocol_init failed\n");
		goto error_control;
	}

	retval = gb_svc_protocol_init();
	if (retval) {
		pr_err("gb_svc_protocol_init failed\n");
		goto error_svc;
	}

	retval = gb_firmware_protocol_init();
	if (retval) {
		pr_err("gb_firmware_protocol_init failed\n");
		goto error_firmware;
	}

	return 0;	/* Success */

error_firmware:
	gb_svc_protocol_exit();
error_svc:
	gb_control_protocol_exit();
error_control:
	gb_endo_exit();
error_endo:
	gb_operation_exit();
error_operation:
	bus_unregister(&greybus_bus_type);
error_bus:
	gb_debugfs_cleanup();

	return retval;
}
module_init(gb_init);

static void __exit gb_exit(void)
{
	gb_firmware_protocol_exit();
	gb_svc_protocol_exit();
	gb_control_protocol_exit();
	gb_endo_exit();
	gb_operation_exit();
	bus_unregister(&greybus_bus_type);
	gb_debugfs_cleanup();
	tracepoint_synchronize_unregister();
}
module_exit(gb_exit);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Greg Kroah-Hartman <gregkh@linuxfoundation.org>");
