/*
 * Simple, generic PCI host controller driver targetting firmware-initialised
 * systems and virtual machines (e.g. the PCI emulation provided by kvmtool).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (C) 2014 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/platform_device.h>

struct gen_pci_cfg_bus_ops {
	u32 bus_shift;
	void __iomem *(*map_bus)(struct pci_bus *, unsigned int, int);
};

struct gen_pci_cfg_windows {
	struct resource				res;
	struct resource				bus_range;
	void __iomem				**win;

	const struct gen_pci_cfg_bus_ops	*ops;
};

struct gen_pci {
	struct pci_host_bridge			host;
	struct gen_pci_cfg_windows		cfg;
	struct list_head			resources;
};

static void __iomem *gen_pci_map_cfg_bus_cam(struct pci_bus *bus,
					     unsigned int devfn,
					     int where)
{
	struct pci_sys_data *sys = bus->sysdata;
	struct gen_pci *pci = sys->private_data;
	resource_size_t idx = bus->number - pci->cfg.bus_range.start;

	return pci->cfg.win[idx] + ((devfn << 8) | where);
}

static struct gen_pci_cfg_bus_ops gen_pci_cfg_cam_bus_ops = {
	.bus_shift	= 16,
	.map_bus	= gen_pci_map_cfg_bus_cam,
};

static void __iomem *gen_pci_map_cfg_bus_ecam(struct pci_bus *bus,
					      unsigned int devfn,
					      int where)
{
	struct pci_sys_data *sys = bus->sysdata;
	struct gen_pci *pci = sys->private_data;
	resource_size_t idx = bus->number - pci->cfg.bus_range.start;

	return pci->cfg.win[idx] + ((devfn << 12) | where);
}

static struct gen_pci_cfg_bus_ops gen_pci_cfg_ecam_bus_ops = {
	.bus_shift	= 20,
	.map_bus	= gen_pci_map_cfg_bus_ecam,
};

static int gen_pci_config_read(struct pci_bus *bus, unsigned int devfn,
				int where, int size, u32 *val)
{
	void __iomem *addr;
	struct pci_sys_data *sys = bus->sysdata;
	struct gen_pci *pci = sys->private_data;

	addr = pci->cfg.ops->map_bus(bus, devfn, where);

	switch (size) {
	case 1:
		*val = readb(addr);
		break;
	case 2:
		*val = readw(addr);
		break;
	default:
		*val = readl(addr);
	}

	return PCIBIOS_SUCCESSFUL;
}

static int gen_pci_config_write(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 val)
{
	void __iomem *addr;
	struct pci_sys_data *sys = bus->sysdata;
	struct gen_pci *pci = sys->private_data;

	addr = pci->cfg.ops->map_bus(bus, devfn, where);

	switch (size) {
	case 1:
		writeb(val, addr);
		break;
	case 2:
		writew(val, addr);
		break;
	default:
		writel(val, addr);
	}

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops gen_pci_ops = {
	.read	= gen_pci_config_read,
	.write	= gen_pci_config_write,
};

static const struct of_device_id gen_pci_of_match[] = {
	{ .compatible = "pci-host-cam-generic",
	  .data = &gen_pci_cfg_cam_bus_ops },

	{ .compatible = "pci-host-ecam-generic",
	  .data = &gen_pci_cfg_ecam_bus_ops },

	{ },
};
MODULE_DEVICE_TABLE(of, gen_pci_of_match);

static int gen_pci_calc_io_offset(struct device *dev,
				  struct of_pci_range *range,
				  struct resource *res,
				  resource_size_t *offset)
{
	static atomic_t wins = ATOMIC_INIT(0);
	int err, idx, max_win;
	unsigned int window;

	if (!PAGE_ALIGNED(range->cpu_addr))
		return -EINVAL;

	max_win = (IO_SPACE_LIMIT + 1) / SZ_64K;
	idx = atomic_inc_return(&wins);
	if (idx > max_win)
		return -ENOSPC;

	window = (idx - 1) * SZ_64K;
	err = pci_ioremap_io(window, range->cpu_addr);
	if (err)
		return err;

	of_pci_range_to_resource(range, dev->of_node, res);
	res->start = window;
	res->end = res->start + range->size - 1;
	*offset = window - range->pci_addr;
	return 0;
}

static int gen_pci_calc_mem_offset(struct device *dev,
				   struct of_pci_range *range,
				   struct resource *res,
				   resource_size_t *offset)
{
	of_pci_range_to_resource(range, dev->of_node, res);
	*offset = range->cpu_addr - range->pci_addr;
	return 0;
}

static void gen_pci_release_of_pci_ranges(struct gen_pci *pci)
{
	struct pci_host_bridge_window *win;

	list_for_each_entry(win, &pci->resources, list)
		release_resource(win->res);

	pci_free_resource_list(&pci->resources);
}

static int gen_pci_parse_request_of_pci_ranges(struct gen_pci *pci)
{
	struct of_pci_range range;
	struct of_pci_range_parser parser;
	int err, res_valid = 0;
	struct device *dev = pci->host.dev.parent;
	struct device_node *np = dev->of_node;

	if (of_pci_range_parser_init(&parser, np)) {
		dev_err(dev, "missing \"ranges\" property\n");
		return -EINVAL;
	}

	for_each_of_pci_range(&parser, &range) {
		struct resource *parent, *res;
		resource_size_t offset;
		u32 restype = range.flags & IORESOURCE_TYPE_BITS;

		res = devm_kmalloc(dev, sizeof(*res), GFP_KERNEL);
		if (!res) {
			err = -ENOMEM;
			goto out_release_res;
		}

		switch (restype) {
		case IORESOURCE_IO:
			parent = &ioport_resource;
			err = gen_pci_calc_io_offset(dev, &range, res, &offset);
			break;
		case IORESOURCE_MEM:
			parent = &iomem_resource;
			err = gen_pci_calc_mem_offset(dev, &range, res, &offset);
			res_valid |= !(res->flags & IORESOURCE_PREFETCH || err);
			break;
		default:
			err = -EINVAL;
			continue;
		}

		if (err) {
			dev_warn(dev,
				 "error %d: failed to add resource [type 0x%x, %lld bytes]\n",
				 err, restype, range.size);
			continue;
		}

		err = request_resource(parent, res);
		if (err)
			goto out_release_res;

		pci_add_resource_offset(&pci->resources, res, offset);
	}

	if (!res_valid) {
		dev_err(dev, "non-prefetchable memory resource required\n");
		err = -EINVAL;
		goto out_release_res;
	}

	return 0;

out_release_res:
	gen_pci_release_of_pci_ranges(pci);
	return err;
}

static int gen_pci_parse_map_cfg_windows(struct gen_pci *pci)
{
	int err;
	u8 bus_max;
	resource_size_t busn;
	struct resource *bus_range;
	struct device *dev = pci->host.dev.parent;
	struct device_node *np = dev->of_node;

	if (of_pci_parse_bus_range(np, &pci->cfg.bus_range))
		pci->cfg.bus_range = (struct resource) {
			.name	= np->name,
			.start	= 0,
			.end	= 0xff,
			.flags	= IORESOURCE_BUS,
		};

	err = of_address_to_resource(np, 0, &pci->cfg.res);
	if (err) {
		dev_err(dev, "missing \"reg\" property\n");
		return err;
	}

	pci->cfg.win = devm_kcalloc(dev, resource_size(&pci->cfg.bus_range),
				    sizeof(*pci->cfg.win), GFP_KERNEL);
	if (!pci->cfg.win)
		return -ENOMEM;

	/* Limit the bus-range to fit within reg */
	bus_max = pci->cfg.bus_range.start +
		  (resource_size(&pci->cfg.res) >> pci->cfg.ops->bus_shift) - 1;
	pci->cfg.bus_range.end = min_t(resource_size_t, pci->cfg.bus_range.end,
				       bus_max);

	/* Map our Configuration Space windows */
	if (!devm_request_mem_region(dev, pci->cfg.res.start,
				     resource_size(&pci->cfg.res),
				     "Configuration Space"))
		return -ENOMEM;

	bus_range = &pci->cfg.bus_range;
	for (busn = bus_range->start; busn <= bus_range->end; ++busn) {
		u32 idx = busn - bus_range->start;
		u32 sz = 1 << pci->cfg.ops->bus_shift;

		pci->cfg.win[idx] = devm_ioremap(dev,
						 pci->cfg.res.start + busn * sz,
						 sz);
		if (!pci->cfg.win[idx])
			return -ENOMEM;
	}

	/* Register bus resource */
	pci_add_resource(&pci->resources, bus_range);
	return 0;
}

static int gen_pci_setup(int nr, struct pci_sys_data *sys)
{
	struct gen_pci *pci = sys->private_data;
	list_splice_init(&pci->resources, &sys->resources);
	return 1;
}

static int gen_pci_probe(struct platform_device *pdev)
{
	int err;
	const char *type;
	const struct of_device_id *of_id;
	const int *prop;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct gen_pci *pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	struct hw_pci hw = {
		.nr_controllers	= 1,
		.private_data	= (void **)&pci,
		.setup		= gen_pci_setup,
		.map_irq	= of_irq_parse_and_map_pci,
		.ops		= &gen_pci_ops,
	};

	if (!pci)
		return -ENOMEM;

	type = of_get_property(np, "device_type", NULL);
	if (!type || strcmp(type, "pci")) {
		dev_err(dev, "invalid \"device_type\" %s\n", type);
		return -EINVAL;
	}

	prop = of_get_property(of_chosen, "linux,pci-probe-only", NULL);
	if (prop) {
		if (*prop)
			pci_add_flags(PCI_PROBE_ONLY);
		else
			pci_clear_flags(PCI_PROBE_ONLY);
	}

	of_id = of_match_node(gen_pci_of_match, np);
	pci->cfg.ops = of_id->data;
	pci->host.dev.parent = dev;
	INIT_LIST_HEAD(&pci->host.windows);
	INIT_LIST_HEAD(&pci->resources);

	/* Parse our PCI ranges and request their resources */
	err = gen_pci_parse_request_of_pci_ranges(pci);
	if (err)
		return err;

	/* Parse and map our Configuration Space windows */
	err = gen_pci_parse_map_cfg_windows(pci);
	if (err) {
		gen_pci_release_of_pci_ranges(pci);
		return err;
	}

	pci_common_init_dev(dev, &hw);
	return 0;
}

static struct platform_driver gen_pci_driver = {
	.driver = {
		.name = "pci-host-generic",
		.owner = THIS_MODULE,
		.of_match_table = gen_pci_of_match,
	},
	.probe = gen_pci_probe,
};
module_platform_driver(gen_pci_driver);

MODULE_DESCRIPTION("Generic PCI host driver");
MODULE_AUTHOR("Will Deacon <will.deacon@arm.com>");
MODULE_LICENSE("GPL v2");
