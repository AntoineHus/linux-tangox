/*
 * Copyright (C) 2014 Mans Rullgard <mans@mansr.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/slab.h>
#include <asm/irq.h>
#include <asm/irq_cpu.h>
#include <asm/io.h>

#define IRQ_CTL_BASE		0x0000
#define FIQ_CTL_BASE		0x0100
#define EDGE_CTL_BASE		0x0200
#define IIQ_CTL_BASE		0x0300

#define IRQ_CTL_HI		0x18
#define EDGE_CTL_HI		0x20

#define IRQ_STATUS		0x00
#define IRQ_RAWSTAT		0x04
#define IRQ_EN_SET		0x08
#define IRQ_EN_CLR		0x0c
#define IRQ_SOFT_SET		0x10
#define IRQ_SOFT_CLR		0x14

#define EDGE_STATUS		0x00
#define EDGE_RAWSTAT		0x04
#define EDGE_CFG_RISE		0x08
#define EDGE_CFG_FALL		0x0c
#define EDGE_CFG_RISE_SET	0x10
#define EDGE_CFG_RISE_CLR	0x14
#define EDGE_CFG_FALL_SET	0x18
#define EDGE_CFG_FALL_CLR	0x1c

struct tangox_irq_chip {
	void __iomem *base;
	unsigned long ctl;
	unsigned int mask;
};

static inline u32 intc_readl(struct tangox_irq_chip *chip, int reg)
{
	return readl(chip->base + reg);
}

static inline void intc_writel(struct tangox_irq_chip *chip, int reg, u32 val)
{
	writel(val, chip->base + reg);
}

static void tangox_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	struct irq_domain *dom = irq_desc_get_handler_data(desc);
	struct tangox_irq_chip *chip = dom->host_data;
	unsigned int status, status_hi;
	unsigned int hwirq;
	unsigned int virq;

	status = intc_readl(chip, chip->ctl + IRQ_STATUS);
	status_hi = intc_readl(chip, chip->ctl + IRQ_CTL_HI + IRQ_STATUS);

	if (!(status | status_hi)) {
		pr_warn("Spurious IRQ %d\n", irq);
		spurious_interrupt();
		return;
	}

	hwirq = status ? __ffs(status) : __ffs(status_hi) + 32;
	virq = irq_find_mapping(dom, hwirq);

	if (chip->mask) {
		unsigned int sr = read_c0_status();
		write_c0_status(sr & ~chip->mask);
		generic_handle_irq(virq);
		write_c0_status(sr);
		return;
	}

	generic_handle_irq(virq);
}

asmlinkage void plat_irq_dispatch(void)
{
	unsigned int pending;

	pending = (read_c0_cause() & read_c0_status() & ST0_IM) >> CAUSEB_IP;

	if (pending)
		do_IRQ(__ffs(pending));
	else
		spurious_interrupt();
}

static void __init tangox_irq_init(struct irq_chip_generic *gc,
				   unsigned long ctl_offs,
				   unsigned long edge_offs)
{
	struct tangox_irq_chip *chip = gc->domain->host_data;
	struct irq_chip_type *ct = gc->chip_types;
	unsigned long ctl_base = chip->ctl + ctl_offs;
	unsigned long edge_base = EDGE_CTL_BASE + edge_offs;

	gc->reg_base = chip->base;
	gc->unused = 0;

	ct->chip.irq_ack = irq_gc_ack_set_bit;
	ct->chip.irq_mask = irq_gc_mask_disable_reg;
	ct->chip.irq_mask_ack = irq_gc_mask_disable_reg_and_ack;
	ct->chip.irq_unmask = irq_gc_unmask_enable_reg;

	ct->regs.enable = ctl_base + IRQ_EN_SET;
	ct->regs.disable = ctl_base + IRQ_EN_CLR;
	ct->regs.ack = edge_base + EDGE_RAWSTAT;
	ct->regs.eoi = ctl_base + IRQ_EN_SET;

	intc_writel(chip, ct->regs.disable, 0xffffffff);
	intc_writel(chip, ct->regs.ack, 0xffffffff);
}

static void __init tangox_irq_domain_init(struct irq_domain *dom)
{
	struct irq_chip_generic *gc;
	int i;

	for (i = 0; i < 2; i++) {
		gc = irq_get_domain_generic_chip(dom, i * 32);
		tangox_irq_init(gc, i * IRQ_CTL_HI, i * EDGE_CTL_HI);
	}
}

static int __init tangox_of_irq_init(struct device_node *node,
				     struct device_node *parent)
{
	struct tangox_irq_chip *chip;
	struct irq_domain *dom;
	struct resource res, ctlres;
	const char *name;
	int irq;
	int err;
	int i;

	irq = irq_of_parse_and_map(node, 0);
	if (!irq)
		panic("Failed to get IRQ");

	if (of_address_to_resource(node, 0, &res))
		panic("Failed to get intc memory resource");

	if (of_address_to_resource(node, 1, &ctlres))
		panic("Failed to get intc memory resource");

	if (of_property_read_string(node, "label", &name))
		name = node->name;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	chip->ctl = ctlres.start - res.start;
	chip->mask = ((1 << (irq - 2)) - 1) << STATUSB_IP2;
	chip->base = ioremap(res.start, resource_size(&res));

	dom = irq_domain_add_linear(node, 64, &irq_generic_chip_ops, chip);
	if (!dom)
		panic("Failed to create irqdomain");

	err = irq_alloc_domain_generic_chips(dom, 32, 1, name, handle_level_irq,
					     0, 0, 0);
	if (err)
		panic("Failed to allocate irqchip");

	tangox_irq_domain_init(dom);

	for (i = 0; i < 64; i++)
		irq_create_mapping(dom, i);

	irq_set_chained_handler(irq, tangox_irq_handler);
	irq_set_handler_data(irq, dom);

	return 0;
}

static struct of_device_id tangox_of_irq_ids[] __initdata = {
	{
		.compatible	= "mti,cpu-interrupt-controller",
		.data		= mips_cpu_intc_init,
	},
	{
		.compatible	= "sigma,smp8640-intc",
		.data		= tangox_of_irq_init,
	},
	{ }
};

void __init arch_init_irq(void)
{
	of_irq_init(tangox_of_irq_ids);
}
