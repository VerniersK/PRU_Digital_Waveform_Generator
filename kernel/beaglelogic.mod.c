#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

__visible struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

static const struct modversion_info ____versions[]
__used
__attribute__((section("__versions"))) = {
	{ 0x7109deb1, __VMLINUX_SYMBOL_STR(module_layout) },
	{ 0x82771811, __VMLINUX_SYMBOL_STR(platform_driver_unregister) },
	{ 0x7ebfd38d, __VMLINUX_SYMBOL_STR(__platform_driver_register) },
	{ 0xbbc3f173, __VMLINUX_SYMBOL_STR(pruss_intc_trigger) },
	{ 0xf4fa543b, __VMLINUX_SYMBOL_STR(arm_copy_to_user) },
	{ 0x19d3b7d8, __VMLINUX_SYMBOL_STR(sysfs_create_group) },
	{ 0x275ef902, __VMLINUX_SYMBOL_STR(__init_waitqueue_head) },
	{ 0xd969361, __VMLINUX_SYMBOL_STR(__mutex_init) },
	{ 0x9aaedb04, __VMLINUX_SYMBOL_STR(misc_register) },
	{ 0x14994742, __VMLINUX_SYMBOL_STR(rproc_boot) },
	{ 0xda4660f2, __VMLINUX_SYMBOL_STR(rproc_set_firmware) },
	{ 0xd6b8e852, __VMLINUX_SYMBOL_STR(request_threaded_irq) },
	{ 0x48723399, __VMLINUX_SYMBOL_STR(platform_get_irq_byname) },
	{ 0xce2387ae, __VMLINUX_SYMBOL_STR(pruss_request_mem_region) },
	{ 0x3dc60547, __VMLINUX_SYMBOL_STR(pruss_rproc_get) },
	{ 0x315b35dc, __VMLINUX_SYMBOL_STR(pruss_get) },
	{ 0x725b5f5f, __VMLINUX_SYMBOL_STR(kmem_cache_alloc_trace) },
	{ 0x57bda53c, __VMLINUX_SYMBOL_STR(kmalloc_caches) },
	{ 0x952d4f27, __VMLINUX_SYMBOL_STR(of_match_device) },
	{ 0xfa7a5f24, __VMLINUX_SYMBOL_STR(_dev_info) },
	{ 0x12da5bb2, __VMLINUX_SYMBOL_STR(__kmalloc) },
	{ 0x54240756, __VMLINUX_SYMBOL_STR(mutex_trylock) },
	{ 0xff178f6, __VMLINUX_SYMBOL_STR(__aeabi_idivmod) },
	{ 0xe707d823, __VMLINUX_SYMBOL_STR(__aeabi_uidiv) },
	{ 0xd0b6d540, __VMLINUX_SYMBOL_STR(devm_kmalloc) },
	{ 0xa46f2f1b, __VMLINUX_SYMBOL_STR(kstrtouint) },
	{ 0x27e1a049, __VMLINUX_SYMBOL_STR(printk) },
	{ 0xf48e6ec, __VMLINUX_SYMBOL_STR(pruss_put) },
	{ 0xb1dc6c49, __VMLINUX_SYMBOL_STR(pruss_rproc_put) },
	{ 0x20194594, __VMLINUX_SYMBOL_STR(pruss_release_mem_region) },
	{ 0xf20dabd8, __VMLINUX_SYMBOL_STR(free_irq) },
	{ 0xe2058d24, __VMLINUX_SYMBOL_STR(rproc_shutdown) },
	{ 0xd86e351c, __VMLINUX_SYMBOL_STR(misc_deregister) },
	{ 0xb1ea493e, __VMLINUX_SYMBOL_STR(sysfs_remove_group) },
	{ 0xe422ab4e, __VMLINUX_SYMBOL_STR(mutex_unlock) },
	{ 0x55fedf45, __VMLINUX_SYMBOL_STR(devm_kfree) },
	{ 0x37a0cba, __VMLINUX_SYMBOL_STR(kfree) },
	{ 0xec9248fd, __VMLINUX_SYMBOL_STR(mutex_lock) },
	{ 0xdb7305a1, __VMLINUX_SYMBOL_STR(__stack_chk_fail) },
	{ 0x1cfb04fa, __VMLINUX_SYMBOL_STR(finish_wait) },
	{ 0x344b7739, __VMLINUX_SYMBOL_STR(prepare_to_wait_event) },
	{ 0x1000e51, __VMLINUX_SYMBOL_STR(schedule) },
	{ 0x622598b1, __VMLINUX_SYMBOL_STR(init_wait_entry) },
	{ 0xf9e73082, __VMLINUX_SYMBOL_STR(scnprintf) },
	{ 0x8f678b07, __VMLINUX_SYMBOL_STR(__stack_chk_guard) },
	{ 0xfa2a45e, __VMLINUX_SYMBOL_STR(__memzero) },
	{ 0x28cc25db, __VMLINUX_SYMBOL_STR(arm_copy_from_user) },
	{ 0x2f2712b3, __VMLINUX_SYMBOL_STR(mem_map) },
	{ 0x2e5810c6, __VMLINUX_SYMBOL_STR(__aeabi_unwind_cpp_pr1) },
	{ 0xc5414778, __VMLINUX_SYMBOL_STR(dev_err) },
	{ 0xd85cd67e, __VMLINUX_SYMBOL_STR(__wake_up) },
	{ 0x3c465949, __VMLINUX_SYMBOL_STR(arm_dma_ops) },
	{ 0xe9f2028c, __VMLINUX_SYMBOL_STR(__dynamic_dev_dbg) },
	{ 0xb1ad28e0, __VMLINUX_SYMBOL_STR(__gnu_mcount_nc) },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=pruss_intc,pruss";

MODULE_ALIAS("of:N*T*Cbeaglelogic,beaglelogic");
MODULE_ALIAS("of:N*T*Cbeaglelogic,beaglelogicC*");

MODULE_INFO(srcversion, "6DCC8838CCDFB99903E15E7");
