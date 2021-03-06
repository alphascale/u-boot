/*
 * Copyright (c) 2014 Google, Inc
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <dm.h>
#include <errno.h>
#include <fdtdec.h>
#include <malloc.h>
#include <spi.h>
#include <dm/device-internal.h>
#include <dm/uclass-internal.h>
#include <dm/root.h>
#include <dm/lists.h>
#include <dm/util.h>

DECLARE_GLOBAL_DATA_PTR;

static int spi_set_speed_mode(struct udevice *bus, int speed, int mode)
{
	struct dm_spi_ops *ops;
	int ret;

	ops = spi_get_ops(bus);
	if (ops->set_speed)
		ret = ops->set_speed(bus, speed);
	else
		ret = -EINVAL;
	if (ret) {
		printf("Cannot set speed (err=%d)\n", ret);
		return ret;
	}

	if (ops->set_mode)
		ret = ops->set_mode(bus, mode);
	else
		ret = -EINVAL;
	if (ret) {
		printf("Cannot set mode (err=%d)\n", ret);
		return ret;
	}

	return 0;
}

int spi_claim_bus(struct spi_slave *slave)
{
	struct udevice *dev = slave->dev;
	struct udevice *bus = dev->parent;
	struct dm_spi_ops *ops = spi_get_ops(bus);
	struct dm_spi_bus *spi = bus->uclass_priv;
	int speed;
	int ret;

	speed = slave->max_hz;
	if (spi->max_hz) {
		if (speed)
			speed = min(speed, spi->max_hz);
		else
			speed = spi->max_hz;
	}
	if (!speed)
		speed = 100000;
	ret = spi_set_speed_mode(bus, speed, slave->mode);
	if (ret)
		return ret;

	return ops->claim_bus ? ops->claim_bus(bus) : 0;
}

void spi_release_bus(struct spi_slave *slave)
{
	struct udevice *dev = slave->dev;
	struct udevice *bus = dev->parent;
	struct dm_spi_ops *ops = spi_get_ops(bus);

	if (ops->release_bus)
		ops->release_bus(bus);
}

int spi_xfer(struct spi_slave *slave, unsigned int bitlen,
	     const void *dout, void *din, unsigned long flags)
{
	struct udevice *dev = slave->dev;
	struct udevice *bus = dev->parent;

	if (bus->uclass->uc_drv->id != UCLASS_SPI)
		return -EOPNOTSUPP;

	return spi_get_ops(bus)->xfer(dev, bitlen, dout, din, flags);
}

int spi_post_bind(struct udevice *dev)
{
	/* Scan the bus for devices */
	return dm_scan_fdt_node(dev, gd->fdt_blob, dev->of_offset, false);
}

int spi_post_probe(struct udevice *dev)
{
	struct dm_spi_bus *spi = dev->uclass_priv;

	spi->max_hz = fdtdec_get_int(gd->fdt_blob, dev->of_offset,
				     "spi-max-frequency", 0);

	return 0;
}

int spi_chip_select(struct udevice *dev)
{
	struct spi_slave *slave = dev_get_parentdata(dev);

	return slave ? slave->cs : -ENOENT;
}

/**
 * spi_find_chip_select() - Find the slave attached to chip select
 *
 * @bus:	SPI bus to search
 * @cs:		Chip select to look for
 * @devp:	Returns the slave device if found
 * @return 0 if found, -ENODEV on error
 */
static int spi_find_chip_select(struct udevice *bus, int cs,
				struct udevice **devp)
{
	struct udevice *dev;

	for (device_find_first_child(bus, &dev); dev;
	     device_find_next_child(&dev)) {
		struct spi_slave store;
		struct spi_slave *slave = dev_get_parentdata(dev);

		if (!slave)  {
			slave = &store;
			spi_ofdata_to_platdata(gd->fdt_blob, dev->of_offset,
					       slave);
		}
		debug("%s: slave=%p, cs=%d\n", __func__, slave,
		      slave ? slave->cs : -1);
		if (slave && slave->cs == cs) {
			*devp = dev;
			return 0;
		}
	}

	return -ENODEV;
}

int spi_cs_is_valid(unsigned int busnum, unsigned int cs)
{
	struct spi_cs_info info;
	struct udevice *bus;
	int ret;

	ret = uclass_find_device_by_seq(UCLASS_SPI, busnum, false, &bus);
	if (ret) {
		debug("%s: No bus %d\n", __func__, busnum);
		return ret;
	}

	return spi_cs_info(bus, cs, &info);
}

int spi_cs_info(struct udevice *bus, uint cs, struct spi_cs_info *info)
{
	struct spi_cs_info local_info;
	struct dm_spi_ops *ops;
	int ret;

	if (!info)
		info = &local_info;

	/* If there is a device attached, return it */
	info->dev = NULL;
	ret = spi_find_chip_select(bus, cs, &info->dev);
	if (!ret)
		return 0;

	/*
	 * Otherwise ask the driver. For the moment we don't have CS info.
	 * When we do we could provide the driver with a helper function
	 * to figure out what chip selects are valid, or just handle the
	 * request.
	 */
	ops = spi_get_ops(bus);
	if (ops->cs_info)
		return ops->cs_info(bus, cs, info);

	/*
	 * We could assume there is at least one valid chip select, but best
	 * to be sure and return an error in this case. The driver didn't
	 * care enough to tell us.
	 */
	return -ENODEV;
}

int spi_bind_device(struct udevice *bus, int cs, const char *drv_name,
		    const char *dev_name, struct udevice **devp)
{
	struct driver *drv;
	int ret;

	drv = lists_driver_lookup_name(drv_name);
	if (!drv) {
		printf("Cannot find driver '%s'\n", drv_name);
		return -ENOENT;
	}
	ret = device_bind(bus, drv, dev_name, NULL, -1, devp);
	if (ret) {
		printf("Cannot create device named '%s' (err=%d)\n",
		       dev_name, ret);
		return ret;
	}

	return 0;
}

int spi_find_bus_and_cs(int busnum, int cs, struct udevice **busp,
			struct udevice **devp)
{
	struct udevice *bus, *dev;
	int ret;

	ret = uclass_find_device_by_seq(UCLASS_SPI, busnum, false, &bus);
	if (ret) {
		debug("%s: No bus %d\n", __func__, busnum);
		return ret;
	}
	ret = spi_find_chip_select(bus, cs, &dev);
	if (ret) {
		debug("%s: No cs %d\n", __func__, cs);
		return ret;
	}
	*busp = bus;
	*devp = dev;

	return ret;
}

int spi_get_bus_and_cs(int busnum, int cs, int speed, int mode,
		       const char *drv_name, const char *dev_name,
		       struct udevice **busp, struct spi_slave **devp)
{
	struct udevice *bus, *dev;
	struct spi_slave *slave;
	bool created = false;
	int ret;

	ret = uclass_get_device_by_seq(UCLASS_SPI, busnum, &bus);
	if (ret) {
		printf("Invalid bus %d (err=%d)\n", busnum, ret);
		return ret;
	}
	ret = spi_find_chip_select(bus, cs, &dev);

	/*
	 * If there is no such device, create one automatically. This means
	 * that we don't need a device tree node or platform data for the
	 * SPI flash chip - we will bind to the correct driver.
	 */
	if (ret == -ENODEV && drv_name) {
		debug("%s: Binding new device '%s', busnum=%d, cs=%d, driver=%s\n",
		      __func__, dev_name, busnum, cs, drv_name);
		ret = spi_bind_device(bus, cs, drv_name, dev_name, &dev);
		if (ret)
			return ret;
		created = true;
	} else if (ret) {
		printf("Invalid chip select %d:%d (err=%d)\n", busnum, cs,
		       ret);
		return ret;
	}

	if (!device_active(dev)) {
		slave = (struct spi_slave *)calloc(1,
						   sizeof(struct spi_slave));
		if (!slave) {
			ret = -ENOMEM;
			goto err;
		}

		ret = spi_ofdata_to_platdata(gd->fdt_blob, dev->of_offset,
					     slave);
		if (ret)
			goto err;
		slave->cs = cs;
		slave->dev = dev;
		ret = device_probe_child(dev, slave);
		free(slave);
		if (ret)
			goto err;
	}

	ret = spi_set_speed_mode(bus, speed, mode);
	if (ret)
		goto err;

	*busp = bus;
	*devp = dev_get_parentdata(dev);
	debug("%s: bus=%p, slave=%p\n", __func__, bus, *devp);

	return 0;

err:
	if (created) {
		device_remove(dev);
		device_unbind(dev);
	}

	return ret;
}

/* Compatibility function - to be removed */
struct spi_slave *spi_setup_slave_fdt(const void *blob, int node,
				      int bus_node)
{
	struct udevice *bus, *dev;
	int ret;

	ret = uclass_get_device_by_of_offset(UCLASS_SPI, bus_node, &bus);
	if (ret)
		return NULL;
	ret = device_get_child_by_of_offset(bus, node, &dev);
	if (ret)
		return NULL;
	return dev_get_parentdata(dev);
}

/* Compatibility function - to be removed */
struct spi_slave *spi_setup_slave(unsigned int busnum, unsigned int cs,
				  unsigned int speed, unsigned int mode)
{
	struct spi_slave *slave;
	struct udevice *dev;
	int ret;

	ret = spi_get_bus_and_cs(busnum, cs, speed, mode, NULL, 0, &dev,
				  &slave);
	if (ret)
		return NULL;

	return slave;
}

void spi_free_slave(struct spi_slave *slave)
{
	device_remove(slave->dev);
	slave->dev = NULL;
}

int spi_ofdata_to_platdata(const void *blob, int node,
			   struct spi_slave *spi)
{
	int mode = 0;

	spi->cs = fdtdec_get_int(blob, node, "reg", -1);
	spi->max_hz = fdtdec_get_int(blob, node, "spi-max-frequency", 0);
	if (fdtdec_get_bool(blob, node, "spi-cpol"))
		mode |= SPI_CPOL;
	if (fdtdec_get_bool(blob, node, "spi-cpha"))
		mode |= SPI_CPHA;
	if (fdtdec_get_bool(blob, node, "spi-cs-high"))
		mode |= SPI_CS_HIGH;
	if (fdtdec_get_bool(blob, node, "spi-half-duplex"))
		mode |= SPI_PREAMBLE;
	spi->mode = mode;

	return 0;
}

UCLASS_DRIVER(spi) = {
	.id		= UCLASS_SPI,
	.name		= "spi",
	.post_bind	= spi_post_bind,
	.post_probe	= spi_post_probe,
	.per_device_auto_alloc_size = sizeof(struct dm_spi_bus),
};

UCLASS_DRIVER(spi_generic) = {
	.id		= UCLASS_SPI_GENERIC,
	.name		= "spi_generic",
};

U_BOOT_DRIVER(spi_generic_drv) = {
	.name		= "spi_generic_drv",
	.id		= UCLASS_SPI_GENERIC,
};
