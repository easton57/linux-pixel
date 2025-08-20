// SPDX-License-Identifier: GPL-2.0
/*
 * Common support functions for Edge TPU ML accelerator host-side ops.
 *
 * Copyright (C) 2019 Google, Inc.
 */

#include <asm/current.h>
#include <asm/page.h>
#include <linux/atomic.h>
#include <linux/compiler.h>
#include <linux/cred.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/uidgid.h>

#include <gcip/gcip-firmware.h>

#include "edgetpu-config.h"
#include "edgetpu-debug.h"
#include "edgetpu-device-group.h"
#include "edgetpu-ikv.h"
#include "edgetpu-internal.h"
#include "edgetpu-kci.h"
#include "edgetpu-mailbox.h"
#include "edgetpu-mmu.h"
#include "edgetpu-pm.h"
#include "edgetpu-soc.h"
#include "edgetpu-sw-watchdog.h"
#include "edgetpu-telemetry.h"
#include "edgetpu-usage-stats.h"
#include "edgetpu-wakelock.h"
#include "edgetpu.h"

/*
 * Module parameter to override in-kernel VII usage found in device-tree.
 * By default in-kernel VII will be enabled if the `use-kernel-vii` property is defined in the
 * device-tree, and disabled otherwise. This behavior can be overridden during insmod by passing
 * "force_ikv=x" for the following values:
 * - 0: Disable in-kernel VII regardless of device-tree
 * - 1: Enable in-kernel VII regardless of device-tree
 * - other: ignored
 */
static int force_ikv = -1;
module_param(force_ikv, int, 0440);

#ifndef EDGETPU_NUM_USE_VII_MAILBOXES
#define EDGETPU_NUM_USE_VII_MAILBOXES EDGETPU_NUM_VII_MAILBOXES
#endif

/* Bits higher than VMA_TYPE_WIDTH are used to carry type specific data, e.g., core id. */
#define VMA_TYPE_WIDTH 16
#define VMA_TYPE(x) ((x) & (BIT_MASK(VMA_TYPE_WIDTH) - 1))
#define VMA_DATA_GET(x) ((x) >> VMA_TYPE_WIDTH)
#define VMA_DATA_SET(x, y) (VMA_TYPE(x) | ((y) << VMA_TYPE_WIDTH))

enum edgetpu_vma_type {
	VMA_INVALID,

	VMA_FULL_CSR,
	VMA_VII_CSR,
	VMA_VII_CMDQ,
	VMA_VII_RESPQ,
	VMA_EXT_CSR,
	VMA_EXT_CMDQ,
	VMA_EXT_RESPQ,
	/* For VMA_LOG and VMA_TRACE, core id is stored in bits higher than VMA_TYPE_WIDTH. */
	VMA_LOG,
	VMA_TRACE,
};

/* type that combines enum edgetpu_vma_type and data in higher bits. */
typedef u32 edgetpu_vma_flags_t;

/* structure to be set to vma->vm_private_data on mmap */
struct edgetpu_vma_private {
	struct edgetpu_client *client;
	edgetpu_vma_flags_t flag;
	/*
	 * vm_private_data is copied when a VMA is split, using this reference
	 * counter to know when should this object be freed.
	 */
	refcount_t count;
};

static atomic_t dev_count = ATOMIC_INIT(-1);

static int edgetpu_mmap_full_csr(struct edgetpu_client *client,
				 struct vm_area_struct *vma)
{
	int ret;
	ulong phys_base, vma_size, map_size;

	if (!uid_eq(current_euid(), GLOBAL_ROOT_UID))
		return -EPERM;
	vma_size = vma->vm_end - vma->vm_start;
	map_size = min_t(ulong, vma_size, client->etdev->regs.size);
	phys_base = client->etdev->regs.phys;
	ret = io_remap_pfn_range(vma, vma->vm_start, phys_base >> PAGE_SHIFT,
				 map_size, vma->vm_page_prot);
	if (ret)
		etdev_dbg(client->etdev,
			  "Error remapping PFN range: %d\n", ret);
	return ret;
}

static edgetpu_vma_flags_t mmap_vma_flag(unsigned long pgoff)
{
	const unsigned long off = pgoff << PAGE_SHIFT;

	switch (off) {
	case 0:
		return VMA_FULL_CSR;
	case EDGETPU_MMAP_CSR_OFFSET:
		return VMA_VII_CSR;
	case EDGETPU_MMAP_CMD_QUEUE_OFFSET:
		return VMA_VII_CMDQ;
	case EDGETPU_MMAP_RESP_QUEUE_OFFSET:
		return VMA_VII_RESPQ;
	case EDGETPU_MMAP_EXT_CSR_OFFSET:
		return VMA_EXT_CSR;
	case EDGETPU_MMAP_EXT_CMD_QUEUE_OFFSET:
		return VMA_EXT_CMDQ;
	case EDGETPU_MMAP_EXT_RESP_QUEUE_OFFSET:
		return VMA_EXT_RESPQ;
	case EDGETPU_MMAP_LOG_BUFFER_OFFSET:
		return VMA_DATA_SET(VMA_LOG, 0);
	case EDGETPU_MMAP_TRACE_BUFFER_OFFSET:
		return VMA_DATA_SET(VMA_TRACE, 0);
#if EDGETPU_MAX_TELEMETRY_BUFFERS > 1
	case EDGETPU_MMAP_LOG1_BUFFER_OFFSET:
		return VMA_DATA_SET(VMA_LOG, 1);
	case EDGETPU_MMAP_TRACE1_BUFFER_OFFSET:
		return VMA_DATA_SET(VMA_TRACE, 1);
#endif /* EDGETPU_MAX_TELEMETRY_BUFFERS > 1 */
#if EDGETPU_MAX_TELEMETRY_BUFFERS > 2
	case EDGETPU_MMAP_LOG2_BUFFER_OFFSET:
		return VMA_DATA_SET(VMA_LOG, 2);
	case EDGETPU_MMAP_TRACE2_BUFFER_OFFSET:
		return VMA_DATA_SET(VMA_TRACE, 2);
#endif /* EDGETPU_MAX_TELEMETRY_BUFFERS > 2 */
#if EDGETPU_MAX_TELEMETRY_BUFFERS > 3
	case EDGETPU_MMAP_LOG3_BUFFER_OFFSET:
		return VMA_DATA_SET(VMA_LOG, 3);
	case EDGETPU_MMAP_TRACE3_BUFFER_OFFSET:
		return VMA_DATA_SET(VMA_TRACE, 3);
#endif /* EDGETPU_MAX_TELEMETRY_BUFFERS > 3 */
	default:
		return VMA_INVALID;
	}
}

/*
 * Returns the wakelock event by VMA type. Returns EDGETPU_WAKELOCK_EVENT_END
 * if the type does not correspond to a wakelock event.
 */
static enum edgetpu_wakelock_event
vma_type_to_wakelock_event(enum edgetpu_vma_type type)
{
	switch (type) {
	case VMA_FULL_CSR:
		return EDGETPU_WAKELOCK_EVENT_FULL_CSR;
	case VMA_VII_CSR:
		return EDGETPU_WAKELOCK_EVENT_MBOX_CSR;
	case VMA_VII_CMDQ:
		return EDGETPU_WAKELOCK_EVENT_CMD_QUEUE;
	case VMA_VII_RESPQ:
		return EDGETPU_WAKELOCK_EVENT_RESP_QUEUE;
	case VMA_EXT_CSR:
		return EDGETPU_WAKELOCK_EVENT_MBOX_CSR;
	case VMA_EXT_CMDQ:
		return EDGETPU_WAKELOCK_EVENT_CMD_QUEUE;
	case VMA_EXT_RESPQ:
		return EDGETPU_WAKELOCK_EVENT_RESP_QUEUE;
	default:
		return EDGETPU_WAKELOCK_EVENT_END;
	}
}

static struct edgetpu_vma_private *
edgetpu_vma_private_alloc(struct edgetpu_client *client,
			  edgetpu_vma_flags_t flag)
{
	struct edgetpu_vma_private *pvt = kmalloc(sizeof(*pvt), GFP_KERNEL);

	if (!pvt)
		return NULL;
	pvt->client = edgetpu_client_get(client);
	pvt->flag = flag;
	refcount_set(&pvt->count, 1);

	return pvt;
}

static void edgetpu_vma_private_get(struct edgetpu_vma_private *pvt)
{
	WARN_ON_ONCE(!refcount_inc_not_zero(&pvt->count));
}

static void edgetpu_vma_private_put(struct edgetpu_vma_private *pvt)
{
	if (!pvt)
		return;
	if (refcount_dec_and_test(&pvt->count)) {
		edgetpu_client_put(pvt->client);
		kfree(pvt);
	}
}

static void edgetpu_vma_open(struct vm_area_struct *vma)
{
	struct edgetpu_vma_private *pvt = vma->vm_private_data;
	enum edgetpu_wakelock_event evt;
	struct edgetpu_client *client;
	struct edgetpu_dev *etdev;
	enum edgetpu_vma_type type = VMA_TYPE(pvt->flag);

	edgetpu_vma_private_get(pvt);
	client = pvt->client;
	etdev = client->etdev;

	evt = vma_type_to_wakelock_event(type);
	if (evt != EDGETPU_WAKELOCK_EVENT_END)
		edgetpu_wakelock_inc_event(&client->wakelock, evt);

	/* handle telemetry types */
	switch (type) {
	case VMA_LOG:
		edgetpu_telemetry_inc_mmap_count(etdev, GCIP_TELEMETRY_LOG,
						 VMA_DATA_GET(pvt->flag));
		break;
	case VMA_TRACE:
		edgetpu_telemetry_inc_mmap_count(etdev, GCIP_TELEMETRY_TRACE,
						 VMA_DATA_GET(pvt->flag));
		break;
	default:
		break;
	}
}

/* Records previously mmapped addresses were unmapped. */
static void edgetpu_vma_close(struct vm_area_struct *vma)
{
	struct edgetpu_vma_private *pvt = vma->vm_private_data;
	struct edgetpu_client *client = pvt->client;
	enum edgetpu_vma_type type = VMA_TYPE(pvt->flag);
	enum edgetpu_wakelock_event evt = vma_type_to_wakelock_event(type);
	struct edgetpu_dev *etdev = client->etdev;

	if (evt != EDGETPU_WAKELOCK_EVENT_END)
		edgetpu_wakelock_dec_event(&client->wakelock, evt);

	/* handle telemetry types */
	switch (type) {
	case VMA_LOG:
		edgetpu_telemetry_dec_mmap_count(etdev, GCIP_TELEMETRY_LOG,
						 VMA_DATA_GET(pvt->flag));
		break;
	case VMA_TRACE:
		edgetpu_telemetry_dec_mmap_count(etdev, GCIP_TELEMETRY_TRACE,
						 VMA_DATA_GET(pvt->flag));
		break;
	default:
		break;
	}

	edgetpu_vma_private_put(pvt);
}

static const struct vm_operations_struct edgetpu_vma_ops = {
	.open = edgetpu_vma_open,
	.close = edgetpu_vma_close,
};

/* Map exported device CSRs or queue into user space. */
int edgetpu_mmap(struct edgetpu_client *client, struct vm_area_struct *vma)
{
	int ret = 0;
	edgetpu_vma_flags_t flag;
	enum edgetpu_vma_type type;
	enum edgetpu_wakelock_event evt;
	struct edgetpu_vma_private *pvt;

	if (vma->vm_start & ~PAGE_MASK) {
		etdev_dbg(client->etdev,
			  "Base address not page-aligned: %#lx\n",
			  vma->vm_start);
		return -EINVAL;
	}

	etdev_dbg(client->etdev, "%s: mmap pgoff = %#lX\n", __func__,
		  vma->vm_pgoff);

	flag = mmap_vma_flag(vma->vm_pgoff);
	type = VMA_TYPE(flag);
	if (type == VMA_INVALID)
		return -EINVAL;
	if (client->etdev->mailbox_manager->use_ikv && type != VMA_LOG && type != VMA_TRACE) {
		etdev_err(client->etdev, "Invalid mmap pgoff (%#lX) for IKV\n", vma->vm_pgoff);
		return -EINVAL;
	}
	pvt = edgetpu_vma_private_alloc(client, flag);
	if (!pvt)
		return -ENOMEM;

	/* Mark the VMA's pages as uncacheable. */
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	/* Disable fancy things to ensure our event counters work. */
	vm_flags_set(vma, VM_DONTCOPY | VM_DONTEXPAND | VM_DONTDUMP);

	/* map all CSRs for debug purpose */
	if (type == VMA_FULL_CSR) {
		evt = EDGETPU_WAKELOCK_EVENT_FULL_CSR;
		if (edgetpu_wakelock_inc_event(&client->wakelock, evt)) {
			ret = edgetpu_mmap_full_csr(client, vma);
			if (ret)
				goto out_dec_evt;
		} else {
			ret = -EAGAIN;
		}
		goto out_set_op;
	}

	/* Allow mapping log and telemetry buffers without a group */
	if (type == VMA_LOG) {
		ret = edgetpu_mmap_telemetry_buffer(client->etdev, GCIP_TELEMETRY_LOG, vma,
						    VMA_DATA_GET(flag));
		goto out_set_op;
	}
	if (type == VMA_TRACE) {
		ret = edgetpu_mmap_telemetry_buffer(client->etdev, GCIP_TELEMETRY_TRACE, vma,
						    VMA_DATA_GET(flag));
		goto out_set_op;
	}

	evt = vma_type_to_wakelock_event(type);
	/*
	 * VMA_TYPE(@flag) should always correspond to a valid event since we handled
	 * telemetry mmaps above, still check evt != END in case new types are
	 * added in the future.
	 */
	if (unlikely(evt == EDGETPU_WAKELOCK_EVENT_END)) {
		ret = -EINVAL;
		goto err_release_pvt;
	}
	if (!edgetpu_wakelock_inc_event(&client->wakelock, evt)) {
		ret = -EAGAIN;
		goto err_release_pvt;
	}

	mutex_lock(&client->group_lock);
	if (!client->group) {
		ret = -EINVAL;
		goto out_unlock;
	}
	switch (type) {
	case VMA_VII_CSR:
		ret = edgetpu_mmap_csr(client->group, vma, false);
		break;
	case VMA_VII_CMDQ:
		ret = edgetpu_mmap_queue(client->group, GCIP_MAILBOX_CMD_QUEUE, vma, false);
		break;
	case VMA_VII_RESPQ:
		ret = edgetpu_mmap_queue(client->group, GCIP_MAILBOX_RESP_QUEUE, vma, false);
		break;
	case VMA_EXT_CSR:
		ret = edgetpu_mmap_csr(client->group, vma, true);
		break;
	case VMA_EXT_CMDQ:
		ret = edgetpu_mmap_queue(client->group, GCIP_MAILBOX_CMD_QUEUE, vma, true);
		break;
	case VMA_EXT_RESPQ:
		ret = edgetpu_mmap_queue(client->group, GCIP_MAILBOX_RESP_QUEUE, vma, true);
		break;
	default: /* to appease compiler */
		break;
	}
out_unlock:
	mutex_unlock(&client->group_lock);
out_dec_evt:
	if (ret)
		edgetpu_wakelock_dec_event(&client->wakelock, evt);
out_set_op:
	if (!ret) {
		vma->vm_private_data = pvt;
		vma->vm_ops = &edgetpu_vma_ops;
		return 0;
	}
err_release_pvt:
	edgetpu_vma_private_put(pvt);
	return ret;
}

int edgetpu_get_state_errno_locked(struct edgetpu_dev *etdev)
{
	switch (etdev->state) {
	case ETDEV_STATE_BAD:
	case ETDEV_STATE_NOFW:
		return -EIO;
	case ETDEV_STATE_FWLOADING:
		return -EAGAIN;
	case ETDEV_STATE_SHUTDOWN:
		return -ESHUTDOWN;
	default:
		break;
	}
	return 0;
}

static struct gcip_fw_tracing *edgetpu_firmware_tracing_create(struct edgetpu_dev *etdev)
{
	const struct gcip_fw_tracing_args fw_tracing_args = {
		.dev = etdev->dev,
		.pm = edgetpu_gcip_pm(etdev),
		.dentry = edgetpu_fs_debugfs_dir(),
		.data = etdev,
		.set_level = edgetpu_kci_firmware_tracing_level,
	};

	return gcip_firmware_tracing_create(&fw_tracing_args);
}

static void edgetpu_firmware_tracing_destroy(struct gcip_fw_tracing *fw_tracing)
{
	gcip_firmware_tracing_destroy(fw_tracing);
}

int edgetpu_device_add(struct edgetpu_dev *etdev,
		       const struct edgetpu_mapped_resource *regs,
		       const struct edgetpu_iface_params *iface_params,
		       uint num_ifaces)
{
	struct edgetpu_mailbox_manager_desc mailbox_manager_desc = {
		.num_mailbox = EDGETPU_NUM_MAILBOXES,
		.num_vii_mailbox = EDGETPU_NUM_VII_MAILBOXES,
		.num_use_vii_mailbox = EDGETPU_NUM_USE_VII_MAILBOXES,
		.num_ext_mailbox = EDGETPU_NUM_EXT_MAILBOXES,
		.get_context_csr_base = edgetpu_mailbox_get_context_csr_base,
		.get_cmd_queue_csr_base = edgetpu_mailbox_get_cmd_queue_csr_base,
		.get_resp_queue_csr_base = edgetpu_mailbox_get_resp_queue_csr_base,
	};
	uint ordinal_id;
	int ret;

	etdev->regs = *regs;

	etdev->etiface = devm_kzalloc(
		etdev->dev, sizeof(*etdev->etiface) * num_ifaces, GFP_KERNEL);

	if (!etdev->etiface) {
		dev_err(etdev->dev,
			"Failed to allocate memory for interfaces\n");
		return -ENOMEM;
	}

	ordinal_id = atomic_add_return(1, &dev_count);

	if (!ordinal_id)
		snprintf(etdev->dev_name, EDGETPU_DEVICE_NAME_MAX, "%s",
			 DRIVER_NAME);
	else
		snprintf(etdev->dev_name, EDGETPU_DEVICE_NAME_MAX,
			 "%s.%u", DRIVER_NAME, ordinal_id);

	mutex_init(&etdev->groups_lock);
	INIT_LIST_HEAD(&etdev->groups);
	etdev->n_groups = 0;
	etdev->group_join_lockout = false;
	mutex_init(&etdev->clients_lock);
	INIT_LIST_HEAD(&etdev->clients);
	etdev->vcid_pool = (1u << EDGETPU_NUM_VCIDS) - 1;
	mutex_init(&etdev->state_lock);
	etdev->state = ETDEV_STATE_NOFW;
	mutex_init(&etdev->device_prop.lock);
	ret = edgetpu_soc_early_init(etdev);
	if (ret)
		return ret;

	ret = edgetpu_fs_add(etdev, iface_params, num_ifaces);
	if (ret) {
		dev_err(etdev->dev, "%s: edgetpu_fs_add returns %d\n", etdev->dev_name, ret);
		goto remove_dev;
	}

	switch (force_ikv) {
	case 1:
		mailbox_manager_desc.use_ikv = true;
		break;
	case 0:
		mailbox_manager_desc.use_ikv = false;
		break;
	default:
		mailbox_manager_desc.use_ikv =
			of_find_property(etdev->dev->of_node, "use-kernel-vii", NULL) != NULL;
	}
	if (mailbox_manager_desc.use_ikv) {
		/* If using in-kernel VII, don't allocate any mailboxes for user-space VII. */
		mailbox_manager_desc.num_vii_mailbox--;
		mailbox_manager_desc.num_use_vii_mailbox = 0;
	}

	etdev->mailbox_manager =
		edgetpu_mailbox_create_mgr(etdev, &mailbox_manager_desc);
	if (IS_ERR(etdev->mailbox_manager)) {
		ret = PTR_ERR(etdev->mailbox_manager);
		dev_err(etdev->dev,
			"%s: edgetpu_mailbox_create_mgr returns %d\n",
			etdev->dev_name, ret);
		goto remove_dev;
	}

	/* Init PM in case the platform needs power up actions before MMU setup and such. */
	ret = edgetpu_pm_create(etdev);
	if (ret) {
		etdev_err(etdev, "Failed to initialize PM interface: %d", ret);
		goto remove_mboxes;
	}

	ret = edgetpu_mmu_attach(etdev);
	if (ret) {
		dev_err(etdev->dev, "failed to attach IOMMU: %d", ret);
		goto remove_pm;
	}

	edgetpu_usage_stats_init(etdev);

	etdev->etkci = devm_kzalloc(etdev->dev, sizeof(*etdev->etkci), GFP_KERNEL);
	if (!etdev->etkci) {
		ret = -ENOMEM;
		goto remove_usage_stats;
	}

	etdev->etikv = devm_kzalloc(etdev->dev, sizeof(*etdev->etikv), GFP_KERNEL);
	if (!etdev->etikv) {
		ret = -ENOMEM;
		goto remove_usage_stats;
	}

	ret = edgetpu_telemetry_init(etdev);
	if (ret)
		goto remove_usage_stats;

	ret = edgetpu_kci_init(etdev->mailbox_manager, etdev->etkci);
	if (ret) {
		etdev_err(etdev, "edgetpu_kci_init returns %d\n", ret);
		goto out_telemetry_exit;
	}

	ret = edgetpu_ikv_init(etdev->mailbox_manager, etdev->etikv);
	if (ret) {
		etdev_err(etdev, "edgetpu_ikv_init returns %d\n", ret);
		goto out_telemetry_exit;
	}

	edgetpu_debug_init(etdev);
	etdev->fw_tracing = edgetpu_firmware_tracing_create(etdev);
	if (IS_ERR(etdev->fw_tracing)) {
		etdev_warn(etdev, "firmware tracing create fail: %ld", PTR_ERR(etdev->fw_tracing));
		etdev->fw_tracing = NULL;
	}

	/* No limit on DMA segment size */
	dma_set_max_seg_size(etdev->dev, UINT_MAX);
	return 0;

out_telemetry_exit:
	edgetpu_telemetry_exit(etdev);
remove_usage_stats:
	edgetpu_usage_stats_exit(etdev);
	edgetpu_mmu_detach(etdev);
remove_pm:
	edgetpu_pm_destroy(etdev);
remove_mboxes:
	edgetpu_mailbox_remove_all(etdev->mailbox_manager, false);
remove_dev:
	edgetpu_fs_remove(etdev);
	edgetpu_soc_exit(etdev);
	return ret;
}

void edgetpu_device_remove(struct edgetpu_dev *etdev)
{
	int ret;

	ret = edgetpu_pm_get(etdev);
	edgetpu_firmware_tracing_destroy(etdev->fw_tracing);
	edgetpu_debug_exit(etdev);
	/* If not known powered up don't try to set mailbox CSRs to disabled state. */
	edgetpu_mailbox_remove_all(etdev->mailbox_manager, !ret);
	edgetpu_telemetry_exit(etdev);
	edgetpu_usage_stats_exit(etdev);
	edgetpu_mmu_detach(etdev);
	if (!ret)
		edgetpu_pm_put(etdev);
	edgetpu_pm_destroy(etdev);
	edgetpu_fs_remove(etdev);
	edgetpu_soc_exit(etdev);
}

struct edgetpu_client *edgetpu_client_add(struct edgetpu_dev_iface *etiface)
{
	struct edgetpu_client *client;
	struct edgetpu_list_device_client *l = kmalloc(sizeof(*l), GFP_KERNEL);
	struct edgetpu_dev *etdev = etiface->etdev;

	if (!l)
		return ERR_PTR(-ENOMEM);
	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client) {
		kfree(l);
		return ERR_PTR(-ENOMEM);
	}
	edgetpu_wakelock_init(etdev, &client->wakelock);
	client->pid = current->pid;
	client->tgid = current->tgid;
	client->etdev = etdev;
	client->etiface = etiface;
	mutex_init(&client->group_lock);
	/* equivalent to edgetpu_client_get() */
	refcount_set(&client->count, 1);
	client->perdie_events = 0;
	mutex_lock(&etdev->clients_lock);
	l->client = client;
	list_add_tail(&l->list, &etdev->clients);
	mutex_unlock(&etdev->clients_lock);
	return client;
}

struct edgetpu_client *edgetpu_client_get(struct edgetpu_client *client)
{
	WARN_ON_ONCE(!refcount_inc_not_zero(&client->count));
	return client;
}

void edgetpu_client_put(struct edgetpu_client *client)
{
	if (!client)
		return;
	if (refcount_dec_and_test(&client->count))
		kfree(client);
}

void edgetpu_client_remove(struct edgetpu_client *client)
{
	struct edgetpu_dev *etdev = client->etdev;
	struct edgetpu_list_device_client *lc;
	uint wakelock_count;

	mutex_lock(&client->group_lock);
	/*
	 * Safe to read wakelock->req_count here since req_count is only modified during
	 * [acquire/release]_wakelock ioctl calls which cannot race with releasing client/fd.
	 */
	wakelock_count = client->wakelock.req_count;
	/*
	 * @wakelock_count = 0 means the device might be powered off. Mailbox(EXT/VII) is removed
	 * when the group is released, so we need to ensure the device should not accessed to
	 * prevent kernel panic on programming mailbox CSRs.
	 */
	if (!wakelock_count && client->group)
		client->group->dev_inaccessible = true;

	mutex_unlock(&client->group_lock);

	mutex_lock(&etdev->clients_lock);
	/* remove the client from the device list */
	for_each_list_device_client(etdev, lc) {
		if (lc->client == client) {
			list_del(&lc->list);
			kfree(lc);
			break;
		}
	}
	mutex_unlock(&etdev->clients_lock);
	/*
	 * A quick check without holding client->group_lock.
	 *
	 * If client doesn't belong to a group then we are fine to not remove
	 * from groups.
	 *
	 * If there is a race that the client belongs to a group but is removing
	 * by another process - this will be detected by the check with holding
	 * client->group_lock later.
	 */
	if (client->group)
		edgetpu_device_group_leave(client);
	/* Cleanup external mailbox/secure client stuff. */
	edgetpu_ext_client_remove(client);

	/* Clean up all the per die event fds registered by the client */
	if (client->perdie_events &
	    1 << perdie_event_id_to_num(EDGETPU_PERDIE_EVENT_LOGS_AVAILABLE))
		edgetpu_telemetry_unset_event(etdev, GCIP_TELEMETRY_LOG);
	if (client->perdie_events &
	    1 << perdie_event_id_to_num(EDGETPU_PERDIE_EVENT_TRACES_AVAILABLE))
		edgetpu_telemetry_unset_event(etdev, GCIP_TELEMETRY_TRACE);

	edgetpu_client_put(client);

	/* Releases each acquired wake lock for this client. */
	while (wakelock_count--)
		edgetpu_pm_put(etdev);
}

void edgetpu_handle_firmware_crash(struct edgetpu_dev *etdev, enum gcip_fw_crash_type crash_type)
{
	if (crash_type == GCIP_FW_CRASH_UNRECOVERABLE_FAULT) {
		etdev_err(etdev, "firmware unrecoverable crash");
		etdev->firmware_crash_count++;
		edgetpu_fatal_error_notify(etdev, EDGETPU_ERROR_FW_CRASH);
		edgetpu_debug_dump(etdev, DUMP_REASON_UNRECOVERABLE_FAULT);
	} else {
		etdev_err(etdev, "firmware non-fatal crash event: %u",
			  crash_type);
		edgetpu_debug_dump(etdev, DUMP_REASON_NON_FATAL_CRASH);
	}
}

int __init edgetpu_init(void)
{
	int ret;

	ret = edgetpu_fs_init();
	if (ret)
		return ret;
	return 0;
}

void __exit edgetpu_exit(void)
{
	edgetpu_fs_exit();
}
