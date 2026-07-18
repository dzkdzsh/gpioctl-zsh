// SPDX-License-Identifier: GPL-2.0-only
#include <linux/bitmap.h>
#include <linux/capability.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/timekeeping.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#include "gpioctl_hal_zsh.h"

#define GPIOCTL_ZSH_NAME "gpioctl_core_zsh"
#define GPIOCTL_ZSH_MAX_CONTROLLERS 256

struct gpioctl_session_zsh;

struct gpioctl_line_state_zsh {
	struct gpioctl_session_zsh *session;
	struct gpioctl_iopad_provider_zsh *iopad_provider;
	void *hal_line;
	u64 iopad_saved_state;
	unsigned int offset;
	int irq;
	u32 edge;
	u32 debounce_us;
	u64 last_event_ns;
	bool leased;
	bool output;
	bool active_low;
	bool input_only;
	int logical_value;
};

struct gpioctl_controller_zsh {
	struct cdev cdev;
	dev_t devt;
	struct device *device;
	struct gpioctl_backend_desc_zsh backend;
	struct gpioctl_line_policy_desc_zsh *policies;
	struct mutex lock;
	unsigned long *leased;
	unsigned int id;
	unsigned int allowlisted_lines;
	unsigned int output_lines;
	unsigned int reserved_lines;
	atomic_t open_count;
	atomic_t active_leases;
	atomic64_t operations;
	atomic64_t errors;
	atomic64_t denials;
	atomic64_t lease_conflicts;
	atomic64_t events;
	atomic64_t event_drops;
	bool unregistering;
};

struct gpioctl_session_zsh {
	struct gpioctl_controller_zsh *controller;
	struct mutex lock;
	struct gpioctl_line_state_zsh *lines;
	spinlock_t event_lock;
	wait_queue_head_t event_wait;
	struct gpioctl_zsh_event *events;
	u32 event_head;
	u32 event_tail;
	u32 event_count;
	u64 event_sequence;
	bool overflow_pending;
	bool closing;
};

struct gpioctl_snapshot_zsh {
	unsigned int offset;
	bool output;
	bool active_low;
	int logical_value;
};

struct gpioctl_iopad_provider_zsh {
	struct gpioctl_iopad_provider_desc_zsh desc;
	atomic_t active_calls;
	wait_queue_head_t wait;
	bool unregistering;
};

static dev_t gpioctl_base_devt_zsh;
static struct class *gpioctl_class_zsh;
static DEFINE_IDA(gpioctl_ida_zsh);
static DEFINE_MUTEX(gpioctl_iopad_lock_zsh);
static struct gpioctl_iopad_provider_zsh *gpioctl_iopad_provider_zsh;

static_assert(sizeof(struct gpioctl_zsh_event) == 48);
static_assert(sizeof(struct gpioctl_zsh_batch_op) == 32);

static bool gpioctl_reserved_zero_zsh(const u32 *reserved, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++)
		if (reserved[i])
			return false;
	return true;
}

static int gpioctl_validate_header_zsh(u32 version, u32 size,
				       size_t expected)
{
	if (version != GPIOCTL_ZSH_ABI_VERSION)
		return -EPROTO;
	if (size != expected)
		return -EINVAL;
	return 0;
}

static int gpioctl_validate_policy_zsh(
	const struct gpioctl_line_policy_desc_zsh *policy)
{
	const u32 known_flags = GPIOCTL_ZSH_POLICY_ALLOW_UNPRIVILEGED |
		GPIOCTL_ZSH_POLICY_OUTPUT_ALLOWED | GPIOCTL_ZSH_POLICY_RESERVED;

	if (policy->flags & ~known_flags ||
	    policy->safe_direction > GPIOCTL_ZSH_DIRECTION_OUTPUT ||
	    policy->safe_value > 1 ||
	    policy->safe_bias < GPIOCTL_ZSH_BIAS_DISABLE ||
	    policy->safe_bias > GPIOCTL_ZSH_BIAS_PULL_DOWN)
		return -EINVAL;
	if ((policy->flags & GPIOCTL_ZSH_POLICY_RESERVED) &&
	    (policy->flags != GPIOCTL_ZSH_POLICY_RESERVED ||
	     policy->safe_direction != GPIOCTL_ZSH_DIRECTION_INPUT ||
	     policy->safe_value))
		return -EINVAL;
	if (policy->safe_direction == GPIOCTL_ZSH_DIRECTION_OUTPUT &&
	    !(policy->flags & GPIOCTL_ZSH_POLICY_OUTPUT_ALLOWED))
		return -EINVAL;
	return 0;
}

static struct gpioctl_iopad_provider_zsh *gpioctl_get_iopad_zsh(void)
{
	struct gpioctl_iopad_provider_zsh *provider;

	mutex_lock(&gpioctl_iopad_lock_zsh);
	provider = gpioctl_iopad_provider_zsh;
	if (provider) {
		if (provider->unregistering ||
		    !try_module_get(provider->desc.owner))
			provider = NULL;
		else
			atomic_inc(&provider->active_calls);
	}
	mutex_unlock(&gpioctl_iopad_lock_zsh);
	return provider;
}

static void gpioctl_put_iopad_zsh(struct gpioctl_iopad_provider_zsh *provider)
{
	if (provider) {
		if (atomic_dec_and_test(&provider->active_calls))
			wake_up_all(&provider->wait);
		module_put(provider->desc.owner);
	}
}

static int gpioctl_prepare_iopad_zsh(
	struct gpioctl_controller_zsh *controller,
	struct gpioctl_line_state_zsh *line)
{
	struct gpioctl_iopad_provider_zsh *provider;
	int ret;

	provider = gpioctl_get_iopad_zsh();
	if (!provider)
		return 0;
	if (!provider->desc.ops->lease_prepare ||
	    !provider->desc.ops->supports(provider->desc.priv,
			controller->backend.hardware_key, line->offset)) {
		gpioctl_put_iopad_zsh(provider);
		return 0;
	}
	ret = provider->desc.ops->lease_prepare(provider->desc.priv,
		controller->backend.hardware_key, line->offset,
		&line->iopad_saved_state);
	if (ret) {
		gpioctl_put_iopad_zsh(provider);
		return ret;
	}
	/* Keep the provider and its module pinned until safe lease release. */
	line->iopad_provider = provider;
	return 0;
}

static int gpioctl_restore_iopad_zsh(
	struct gpioctl_controller_zsh *controller,
	struct gpioctl_line_state_zsh *line)
{
	struct gpioctl_iopad_provider_zsh *provider = line->iopad_provider;
	int ret = 0;

	if (!provider)
		return 0;
	ret = provider->desc.ops->lease_restore(provider->desc.priv,
		controller->backend.hardware_key, line->offset,
		line->iopad_saved_state);
	line->iopad_provider = NULL;
	line->iopad_saved_state = 0;
	gpioctl_put_iopad_zsh(provider);
	return ret;
}

static int gpioctl_read_iopad_zsh(struct gpioctl_controller_zsh *controller,
				  unsigned int offset,
				  struct gpioctl_zsh_iopad_config *config)
{
	struct gpioctl_iopad_provider_zsh *provider;
	int ret;

	if (controller->backend.ops->get_iopad)
		return controller->backend.ops->get_iopad(
			controller->backend.priv, offset, config);
	provider = gpioctl_get_iopad_zsh();
	if (!provider)
		return -EOPNOTSUPP;
	if (!provider->desc.ops->supports(provider->desc.priv,
			controller->backend.hardware_key, offset)) {
		gpioctl_put_iopad_zsh(provider);
		return -EOPNOTSUPP;
	}
	ret = provider->desc.ops->get_config(provider->desc.priv,
		controller->backend.hardware_key, offset, config);
	gpioctl_put_iopad_zsh(provider);
	return ret;
}

static int gpioctl_set_bias_zsh(struct gpioctl_controller_zsh *controller,
				struct gpioctl_line_state_zsh *line,
				enum gpioctl_zsh_bias bias)
{
	struct gpioctl_zsh_iopad_config config = {
		.abi_version = GPIOCTL_ZSH_ABI_VERSION,
		.struct_size = sizeof(config),
		.flags = GPIOCTL_ZSH_IOPAD_APPLY_BIAS,
		.bias = bias,
	};
	struct gpioctl_iopad_provider_zsh *provider;
	int ret = -EOPNOTSUPP;

	if (controller->backend.ops->set_bias) {
		ret = controller->backend.ops->set_bias(
			controller->backend.priv, line->hal_line, bias);
		if (ret != -ENOTSUPP && ret != -EOPNOTSUPP)
			return ret;
	}

	provider = gpioctl_get_iopad_zsh();
	if (!provider)
		return ret;
	if (!provider->desc.ops->supports(provider->desc.priv,
			controller->backend.hardware_key, line->offset)) {
		gpioctl_put_iopad_zsh(provider);
		return ret;
	}
	ret = provider->desc.ops->configure(provider->desc.priv,
		controller->backend.hardware_key, line->offset, &config);
	gpioctl_put_iopad_zsh(provider);
	return ret;
}

static int gpioctl_physical_value_zsh(const struct gpioctl_line_state_zsh *line,
				      int logical)
{
	return (!!logical) ^ line->active_low;
}

static int gpioctl_logical_value_zsh(const struct gpioctl_line_state_zsh *line,
				     int physical)
{
	return (!!physical) ^ line->active_low;
}

static int gpioctl_verify_value_zsh(
	struct gpioctl_controller_zsh *controller,
	struct gpioctl_line_state_zsh *line, int logical)
{
	int physical = controller->backend.ops->get_value(
		controller->backend.priv, line->hal_line);

	if (physical < 0)
		return physical;
	return gpioctl_logical_value_zsh(line, physical) == !!logical ? 0 : -EIO;
}

static struct gpioctl_line_state_zsh *
gpioctl_owned_line_zsh(struct gpioctl_session_zsh *session, unsigned int offset)
{
	if (offset >= session->controller->backend.line_count)
		return NULL;
	if (!session->lines[offset].leased)
		return NULL;
	return &session->lines[offset];
}

static void gpioctl_push_event_zsh(struct gpioctl_line_state_zsh *line,
				   u32 edge, u64 now_ns)
{
	struct gpioctl_session_zsh *session = line->session;
	struct gpioctl_controller_zsh *controller = session->controller;
	struct gpioctl_zsh_event *event;
	unsigned long irq_flags;

	spin_lock_irqsave(&session->event_lock, irq_flags);
	if (session->closing) {
		spin_unlock_irqrestore(&session->event_lock, irq_flags);
		return;
	}
	if (line->debounce_us && line->last_event_ns &&
	    now_ns - line->last_event_ns < (u64)line->debounce_us * NSEC_PER_USEC) {
		spin_unlock_irqrestore(&session->event_lock, irq_flags);
		return;
	}
	line->last_event_ns = now_ns;
	if (session->event_count == GPIOCTL_ZSH_EVENT_QUEUE_SIZE) {
		session->event_tail = (session->event_tail + 1) %
			GPIOCTL_ZSH_EVENT_QUEUE_SIZE;
		session->event_count--;
		session->overflow_pending = true;
		atomic64_inc(&controller->event_drops);
	}
	event = &session->events[session->event_head];
	memset(event, 0, sizeof(*event));
	event->abi_version = GPIOCTL_ZSH_ABI_VERSION;
	event->struct_size = sizeof(*event);
	event->offset = line->offset;
	event->edge = edge;
	event->timestamp_ns = now_ns;
	event->sequence = ++session->event_sequence;
	if (session->overflow_pending) {
		event->flags |= GPIOCTL_ZSH_EVENT_OVERFLOW;
		session->overflow_pending = false;
	}
	session->event_head = (session->event_head + 1) %
		GPIOCTL_ZSH_EVENT_QUEUE_SIZE;
	session->event_count++;
	atomic64_inc(&controller->events);
	spin_unlock_irqrestore(&session->event_lock, irq_flags);
	wake_up_interruptible(&session->event_wait);
}

static irqreturn_t gpioctl_irq_thread_zsh(int irq, void *data)
{
	struct gpioctl_line_state_zsh *line = data;
	struct gpioctl_controller_zsh *controller = line->session->controller;
	int value;
	u32 edge;

	value = controller->backend.ops->get_value(controller->backend.priv,
						 line->hal_line);
	if (value < 0)
		return IRQ_NONE;
	value = gpioctl_logical_value_zsh(line, value);
	edge = value ? GPIOCTL_ZSH_EDGE_RISING : GPIOCTL_ZSH_EDGE_FALLING;
	if (!(line->edge & edge))
		return IRQ_NONE;
	gpioctl_push_event_zsh(line, edge, ktime_get_ns());
	return IRQ_HANDLED;
}

static void gpioctl_disable_event_zsh(struct gpioctl_line_state_zsh *line)
{
	if (line->irq >= 0) {
		free_irq(line->irq, line);
		line->irq = -1;
	}
	line->edge = GPIOCTL_ZSH_EDGE_NONE;
	line->debounce_us = 0;
	line->last_event_ns = 0;
}

static int gpioctl_apply_snapshot_zsh(struct gpioctl_controller_zsh *controller,
				      struct gpioctl_line_state_zsh *line,
				      const struct gpioctl_snapshot_zsh *snapshot)
{
	int ret;

	line->active_low = snapshot->active_low;
	if (snapshot->output) {
		ret = controller->backend.ops->direction_output(
			controller->backend.priv, line->hal_line,
			gpioctl_physical_value_zsh(line, snapshot->logical_value));
	} else {
		ret = controller->backend.ops->direction_input(
			controller->backend.priv, line->hal_line);
	}
	if (!ret) {
		line->output = snapshot->output;
		line->logical_value = snapshot->logical_value;
	}
	return ret;
}

static int gpioctl_apply_safe_state_zsh(
	struct gpioctl_controller_zsh *controller,
	struct gpioctl_line_state_zsh *line)
{
	const struct gpioctl_line_policy_desc_zsh *policy =
		&controller->policies[line->offset];
	int first_error = 0;
	int ret;

	if (policy->safe_direction == GPIOCTL_ZSH_DIRECTION_OUTPUT) {
		ret = gpioctl_set_bias_zsh(controller, line, policy->safe_bias);
		if (ret)
			first_error = ret;
		ret = controller->backend.ops->direction_output(
			controller->backend.priv, line->hal_line,
			policy->safe_value);
	} else {
		ret = controller->backend.ops->direction_input(
			controller->backend.priv, line->hal_line);
		if (!ret)
			ret = gpioctl_set_bias_zsh(controller, line,
					       policy->safe_bias);
	}
	if (!first_error)
		first_error = ret;
	return first_error;
}

/* Lock order: controller->lock, then session->lock, then event_lock. */
static int gpioctl_release_line_locked_zsh(
	struct gpioctl_session_zsh *session,
	struct gpioctl_line_state_zsh *line)
{
	struct gpioctl_controller_zsh *controller = session->controller;
	int ret;
	int restore_ret;

	gpioctl_disable_event_zsh(line);
	ret = gpioctl_apply_safe_state_zsh(controller, line);
	restore_ret = gpioctl_restore_iopad_zsh(controller, line);
	if (!ret)
		ret = restore_ret;
	controller->backend.ops->release(controller->backend.priv, line->hal_line);
	clear_bit(line->offset, controller->leased);
	atomic_dec(&controller->active_leases);

	memset(line, 0, sizeof(*line));
	line->offset = line - session->lines;
	line->irq = -1;
	line->session = session;
	return ret;
}

static int gpioctl_open_zsh(struct inode *inode, struct file *file)
{
	struct gpioctl_controller_zsh *controller;
	struct gpioctl_session_zsh *session;
	unsigned int i;

	controller = container_of(inode->i_cdev, struct gpioctl_controller_zsh,
				  cdev);
	mutex_lock(&controller->lock);
	if (controller->unregistering) {
		mutex_unlock(&controller->lock);
		return -ENODEV;
	}
	if (!try_module_get(controller->backend.owner)) {
		mutex_unlock(&controller->lock);
		return -ENODEV;
	}
	atomic_inc(&controller->open_count);
	mutex_unlock(&controller->lock);

	session = kzalloc(sizeof(*session), GFP_KERNEL);
	if (!session)
		goto err_open;
	session->lines = kcalloc(controller->backend.line_count,
				 sizeof(*session->lines), GFP_KERNEL);
	if (!session->lines)
		goto err_session;
	session->events = kcalloc(GPIOCTL_ZSH_EVENT_QUEUE_SIZE,
				  sizeof(*session->events), GFP_KERNEL);
	if (!session->events)
		goto err_lines;

	session->controller = controller;
	mutex_init(&session->lock);
	spin_lock_init(&session->event_lock);
	init_waitqueue_head(&session->event_wait);
	for (i = 0; i < controller->backend.line_count; i++) {
		session->lines[i].session = session;
		session->lines[i].offset = i;
		session->lines[i].irq = -1;
	}
	file->private_data = session;
	return 0;

err_lines:
	kfree(session->lines);
err_session:
	kfree(session);
err_open:
	atomic_dec(&controller->open_count);
	module_put(controller->backend.owner);
	return -ENOMEM;
}

static int gpioctl_release_zsh(struct inode *inode, struct file *file)
{
	struct gpioctl_session_zsh *session = file->private_data;
	struct gpioctl_controller_zsh *controller;
	unsigned long irq_flags;
	unsigned int i;

	if (!session)
		return 0;
	controller = session->controller;
	spin_lock_irqsave(&session->event_lock, irq_flags);
	session->closing = true;
	spin_unlock_irqrestore(&session->event_lock, irq_flags);
	wake_up_interruptible(&session->event_wait);

	mutex_lock(&controller->lock);
	mutex_lock(&session->lock);
	for (i = 0; i < controller->backend.line_count; i++) {
		int ret;

		if (!session->lines[i].leased)
			continue;
		ret = gpioctl_release_line_locked_zsh(session, &session->lines[i]);
		if (ret)
			pr_err_ratelimited(GPIOCTL_ZSH_NAME
				": safe release failed controller=%u line=%u err=%d\n",
				controller->id, i, ret);
	}
	mutex_unlock(&session->lock);
	mutex_unlock(&controller->lock);

	kfree(session->events);
	kfree(session->lines);
	kfree(session);
	atomic_dec(&controller->open_count);
	module_put(controller->backend.owner);
	return 0;
}

static int gpioctl_copy_lease_zsh(void __user *arg,
				 struct gpioctl_zsh_lease *request)
{
	unsigned int i, j;
	int ret;

	if (copy_from_user(request, arg, sizeof(*request)))
		return -EFAULT;
	ret = gpioctl_validate_header_zsh(request->abi_version,
					  request->struct_size,
					  sizeof(*request));
	if (ret)
		return ret;
	if (!gpioctl_reserved_zero_zsh(request->reserved,
					ARRAY_SIZE(request->reserved)))
		return -EINVAL;
	if (!request->count || request->count > GPIOCTL_ZSH_MAX_LINES)
		return -EINVAL;
	if (request->flags & ~GPIOCTL_ZSH_LEASE_INPUT_ONLY)
		return -EINVAL;
	for (i = 0; i < request->count; i++)
		for (j = i + 1; j < request->count; j++)
			if (request->offsets[i] == request->offsets[j])
				return -EINVAL;
	return 0;
}

static int gpioctl_lease_request_zsh(struct gpioctl_session_zsh *session,
				     void __user *arg)
{
	struct gpioctl_controller_zsh *controller = session->controller;
	struct gpioctl_zsh_lease request;
	unsigned int i, acquired = 0;
	bool privileged;
	int ret;

	ret = gpioctl_copy_lease_zsh(arg, &request);
	if (ret)
		return ret;
	privileged = capable(CAP_SYS_RAWIO);
	for (i = 0; i < request.count; i++) {
		const struct gpioctl_line_policy_desc_zsh *policy;

		if (request.offsets[i] >= controller->backend.line_count)
			return -EINVAL;
		policy = &controller->policies[request.offsets[i]];
		if (policy->flags & GPIOCTL_ZSH_POLICY_RESERVED)
			return -EPERM;
		if (!privileged &&
		    !(policy->flags & GPIOCTL_ZSH_POLICY_ALLOW_UNPRIVILEGED))
			return -EACCES;
	}

	mutex_lock(&controller->lock);
	mutex_lock(&session->lock);
	if (controller->unregistering) {
		ret = -ENODEV;
		goto out_unlock;
	}
	for (i = 0; i < request.count; i++) {
		if (test_bit(request.offsets[i], controller->leased)) {
			atomic64_inc(&controller->lease_conflicts);
			ret = -EBUSY;
			goto out_unlock;
		}
	}
	for (i = 0; i < request.count; i++) {
		struct gpioctl_line_state_zsh *line =
			&session->lines[request.offsets[i]];

		ret = controller->backend.ops->request(controller->backend.priv,
						       request.offsets[i],
						       &line->hal_line);
		if (ret)
			goto rollback;
		ret = gpioctl_prepare_iopad_zsh(controller, line);
		if (ret) {
			controller->backend.ops->release(controller->backend.priv,
						 line->hal_line);
			line->hal_line = NULL;
			goto rollback;
		}
		line->leased = true;
		line->output = false;
		line->active_low = false;
		line->input_only = !!(request.flags &
					  GPIOCTL_ZSH_LEASE_INPUT_ONLY);
		set_bit(line->offset, controller->leased);
		atomic_inc(&controller->active_leases);
		acquired++;
	}
	ret = 0;
	goto out_unlock;

rollback:
	while (acquired) {
		struct gpioctl_line_state_zsh *line;

		acquired--;
		line = &session->lines[request.offsets[acquired]];
		if (gpioctl_apply_safe_state_zsh(controller, line))
			pr_err_ratelimited(GPIOCTL_ZSH_NAME
				": lease rollback safe-state failed controller=%u line=%u\n",
				controller->id, line->offset);
		if (gpioctl_restore_iopad_zsh(controller, line))
			pr_err_ratelimited(GPIOCTL_ZSH_NAME
				": lease rollback IOPAD restore failed controller=%u line=%u\n",
				controller->id, line->offset);
		controller->backend.ops->release(controller->backend.priv,
						 line->hal_line);
		clear_bit(line->offset, controller->leased);
		atomic_dec(&controller->active_leases);
		line->hal_line = NULL;
		line->leased = false;
	}
out_unlock:
	mutex_unlock(&session->lock);
	mutex_unlock(&controller->lock);
	return ret;
}

static int gpioctl_lease_release_zsh(struct gpioctl_session_zsh *session,
				     void __user *arg)
{
	struct gpioctl_zsh_lease request;
	unsigned int i;
	int ret;

	ret = gpioctl_copy_lease_zsh(arg, &request);
	if (ret)
		return ret;

	mutex_lock(&session->controller->lock);
	mutex_lock(&session->lock);
	for (i = 0; i < request.count; i++)
		if (!gpioctl_owned_line_zsh(session, request.offsets[i])) {
			ret = -EPERM;
			goto out;
		}
	ret = 0;
	for (i = 0; i < request.count; i++) {
		int release_ret = gpioctl_release_line_locked_zsh(
			session, &session->lines[request.offsets[i]]);

		if (!ret && release_ret)
			ret = release_ret;
	}
out:
	mutex_unlock(&session->lock);
	mutex_unlock(&session->controller->lock);
	return ret;
}

static int gpioctl_config_line_locked_zsh(
	struct gpioctl_session_zsh *session,
	const struct gpioctl_zsh_line_config *config)
{
	struct gpioctl_controller_zsh *controller = session->controller;
	struct gpioctl_line_state_zsh *line;
	struct gpioctl_snapshot_zsh old;
	int ret;

	line = gpioctl_owned_line_zsh(session, config->offset);
	if (!line)
		return -EPERM;
	if (config->direction == GPIOCTL_ZSH_DIRECTION_OUTPUT &&
	    (line->input_only ||
	     !(controller->policies[line->offset].flags &
	       GPIOCTL_ZSH_POLICY_OUTPUT_ALLOWED)))
		return -EPERM;
	old.offset = line->offset;
	old.output = line->output;
	old.active_low = line->active_low;
	old.logical_value = line->logical_value;

	if (config->bias != GPIOCTL_ZSH_BIAS_AS_IS) {
		ret = gpioctl_set_bias_zsh(controller, line, config->bias);
		if (ret)
			return ret;
	}
	line->active_low = !!(config->flags & GPIOCTL_ZSH_LINE_ACTIVE_LOW);
	if (config->debounce_us) {
		if (controller->backend.ops->set_debounce) {
			ret = controller->backend.ops->set_debounce(
				controller->backend.priv, line->hal_line,
				config->debounce_us);
			if (ret && ret != -ENOTSUPP && ret != -EOPNOTSUPP)
				goto rollback;
		}
		line->debounce_us = config->debounce_us;
	}

	if (config->direction == GPIOCTL_ZSH_DIRECTION_OUTPUT) {
		ret = controller->backend.ops->direction_output(
			controller->backend.priv, line->hal_line,
			gpioctl_physical_value_zsh(line, config->value));
		if (!ret) {
			line->output = true;
			line->logical_value = !!config->value;
			ret = gpioctl_verify_value_zsh(controller, line,
						 config->value);
		}
	} else {
		ret = controller->backend.ops->direction_input(
			controller->backend.priv, line->hal_line);
		if (!ret)
			line->output = false;
	}
	if (!ret)
		return 0;

rollback:
	if (gpioctl_apply_snapshot_zsh(controller, line, &old))
		pr_err_ratelimited(GPIOCTL_ZSH_NAME
			": rollback failed controller=%u line=%u\n",
			controller->id, line->offset);
	return ret;
}

static int gpioctl_line_config_zsh(struct gpioctl_session_zsh *session,
				   void __user *arg)
{
	struct gpioctl_zsh_line_config config;
	int ret;

	if (copy_from_user(&config, arg, sizeof(config)))
		return -EFAULT;
	ret = gpioctl_validate_header_zsh(config.abi_version,
					  config.struct_size, sizeof(config));
	if (ret)
		return ret;
	if (!gpioctl_reserved_zero_zsh(config.reserved,
					ARRAY_SIZE(config.reserved)) ||
	    config.flags & ~GPIOCTL_ZSH_LINE_ACTIVE_LOW ||
	    config.direction > GPIOCTL_ZSH_DIRECTION_OUTPUT ||
	    config.value > 1 || config.bias > GPIOCTL_ZSH_BIAS_PULL_DOWN)
		return -EINVAL;

	mutex_lock(&session->lock);
	ret = gpioctl_config_line_locked_zsh(session, &config);
	mutex_unlock(&session->lock);
	return ret;
}

static int gpioctl_copy_values_zsh(void __user *arg,
				  struct gpioctl_zsh_values *values)
{
	unsigned int i, j;
	int ret;

	if (copy_from_user(values, arg, sizeof(*values)))
		return -EFAULT;
	ret = gpioctl_validate_header_zsh(values->abi_version,
					  values->struct_size,
					  sizeof(*values));
	if (ret)
		return ret;
	if (!gpioctl_reserved_zero_zsh(values->reserved,
					ARRAY_SIZE(values->reserved)) ||
	    values->flags || !values->count ||
	    values->count > GPIOCTL_ZSH_MAX_LINES)
		return -EINVAL;
	for (i = 0; i < values->count; i++)
		for (j = i + 1; j < values->count; j++)
			if (values->offsets[i] == values->offsets[j])
				return -EINVAL;
	return 0;
}

static int gpioctl_values_get_zsh(struct gpioctl_session_zsh *session,
				  void __user *arg)
{
	struct gpioctl_controller_zsh *controller = session->controller;
	struct gpioctl_zsh_values values;
	unsigned int i;
	int ret, physical;

	ret = gpioctl_copy_values_zsh(arg, &values);
	if (ret)
		return ret;
	values.values = 0;
	mutex_lock(&session->lock);
	for (i = 0; i < values.count; i++) {
		struct gpioctl_line_state_zsh *line =
			gpioctl_owned_line_zsh(session, values.offsets[i]);

		if (!line) {
			ret = -EPERM;
			goto out;
		}
		physical = controller->backend.ops->get_value(
			controller->backend.priv, line->hal_line);
		if (physical < 0) {
			ret = physical;
			goto out;
		}
		if (gpioctl_logical_value_zsh(line, physical))
			values.values |= BIT(i);
	}
	ret = 0;
out:
	mutex_unlock(&session->lock);
	if (!ret && copy_to_user(arg, &values, sizeof(values)))
		ret = -EFAULT;
	return ret;
}

static int gpioctl_values_set_zsh(struct gpioctl_session_zsh *session,
				  void __user *arg)
{
	struct gpioctl_controller_zsh *controller = session->controller;
	struct gpioctl_zsh_values values;
	int old_values[GPIOCTL_ZSH_MAX_LINES];
	unsigned int i, applied = 0;
	int ret;

	ret = gpioctl_copy_values_zsh(arg, &values);
	if (ret)
		return ret;
	mutex_lock(&session->lock);
	for (i = 0; i < values.count; i++) {
		struct gpioctl_line_state_zsh *line =
			gpioctl_owned_line_zsh(session, values.offsets[i]);

		if (!line || !line->output) {
			ret = -EPERM;
			goto out;
		}
		old_values[i] = line->logical_value;
	}
	for (i = 0; i < values.count; i++) {
		struct gpioctl_line_state_zsh *line =
			&session->lines[values.offsets[i]];
		int logical = !!(values.values & BIT(i));

		ret = controller->backend.ops->set_value(
			controller->backend.priv, line->hal_line,
			gpioctl_physical_value_zsh(line, logical));
		if (!ret)
			ret = gpioctl_verify_value_zsh(controller, line, logical);
		if (ret)
			goto rollback;
		line->logical_value = logical;
		applied++;
	}
	ret = 0;
	goto out;

rollback:
	while (applied) {
		struct gpioctl_line_state_zsh *line;

		applied--;
		line = &session->lines[values.offsets[applied]];
		controller->backend.ops->set_value(
			controller->backend.priv, line->hal_line,
			gpioctl_physical_value_zsh(line, old_values[applied]));
		line->logical_value = old_values[applied];
	}
out:
	mutex_unlock(&session->lock);
	return ret;
}

static int gpioctl_batch_exec_zsh(struct gpioctl_session_zsh *session,
				  void __user *arg)
{
	struct gpioctl_controller_zsh *controller = session->controller;
	struct gpioctl_zsh_batch batch;
	struct gpioctl_snapshot_zsh snapshots[GPIOCTL_ZSH_MAX_BATCH_OPS];
	unsigned int i, j, applied = 0;
	int ret = 0;

	if (copy_from_user(&batch, arg, sizeof(batch)))
		return -EFAULT;
	ret = gpioctl_validate_header_zsh(batch.abi_version, batch.struct_size,
					  sizeof(batch));
	if (ret)
		return ret;
	if (!gpioctl_reserved_zero_zsh(batch.reserved,
					ARRAY_SIZE(batch.reserved)) ||
	    batch.flags || !batch.count || batch.count > GPIOCTL_ZSH_MAX_BATCH_OPS)
		return -EINVAL;
	for (i = 0; i < batch.count; i++) {
		if (!gpioctl_reserved_zero_zsh(batch.ops[i].reserved,
						ARRAY_SIZE(batch.ops[i].reserved)) ||
		    (batch.ops[i].opcode != GPIOCTL_ZSH_BATCH_CONFIG &&
		     batch.ops[i].opcode != GPIOCTL_ZSH_BATCH_SET))
			return -EINVAL;
		for (j = i + 1; j < batch.count; j++)
			if (batch.ops[i].offset == batch.ops[j].offset)
				return -EINVAL;
	}

	batch.failed_index = -1;
	batch.rollback_error = 0;
	mutex_lock(&session->lock);
	for (i = 0; i < batch.count; i++) {
		struct gpioctl_line_state_zsh *line =
			gpioctl_owned_line_zsh(session, batch.ops[i].offset);

		if (!line) {
			ret = -EPERM;
			batch.failed_index = i;
			goto out;
		}
		if (batch.ops[i].opcode == GPIOCTL_ZSH_BATCH_SET &&
		    (!line->output || batch.ops[i].arg0 > 1 ||
		     batch.ops[i].arg1 || batch.ops[i].flags)) {
			ret = -EINVAL;
			batch.failed_index = i;
			goto out;
		}
		if (batch.ops[i].opcode == GPIOCTL_ZSH_BATCH_CONFIG &&
		    (batch.ops[i].arg0 > GPIOCTL_ZSH_DIRECTION_OUTPUT ||
		     batch.ops[i].arg1 > 1 ||
		     batch.ops[i].flags & ~GPIOCTL_ZSH_LINE_ACTIVE_LOW)) {
			ret = -EINVAL;
			batch.failed_index = i;
			goto out;
		}
		if (batch.ops[i].opcode == GPIOCTL_ZSH_BATCH_CONFIG &&
		    batch.ops[i].arg0 == GPIOCTL_ZSH_DIRECTION_OUTPUT &&
		    (line->input_only ||
		     !(controller->policies[line->offset].flags &
		       GPIOCTL_ZSH_POLICY_OUTPUT_ALLOWED))) {
			ret = -EPERM;
			batch.failed_index = i;
			goto out;
		}
		snapshots[i].offset = line->offset;
		snapshots[i].output = line->output;
		snapshots[i].active_low = line->active_low;
		snapshots[i].logical_value = line->logical_value;
	}
	for (i = 0; i < batch.count; i++) {
		struct gpioctl_line_state_zsh *line =
			&session->lines[batch.ops[i].offset];

		if (batch.ops[i].opcode == GPIOCTL_ZSH_BATCH_SET) {
			ret = controller->backend.ops->set_value(
				controller->backend.priv, line->hal_line,
				gpioctl_physical_value_zsh(line,
							   batch.ops[i].arg0));
			if (!ret) {
				line->logical_value = batch.ops[i].arg0;
				ret = gpioctl_verify_value_zsh(controller, line,
							       batch.ops[i].arg0);
			}
		} else {
			struct gpioctl_zsh_line_config config = {
				.abi_version = GPIOCTL_ZSH_ABI_VERSION,
				.struct_size = sizeof(config),
				.offset = batch.ops[i].offset,
				.direction = batch.ops[i].arg0,
				.value = batch.ops[i].arg1,
				.bias = GPIOCTL_ZSH_BIAS_AS_IS,
				.flags = batch.ops[i].flags,
			};

			ret = gpioctl_config_line_locked_zsh(session, &config);
		}
		if (ret) {
			batch.failed_index = i;
			goto rollback;
		}
		applied++;
	}
	goto out;

rollback:
	while (applied) {
		int rollback_ret;
		struct gpioctl_line_state_zsh *line;

		applied--;
		line = &session->lines[snapshots[applied].offset];
		rollback_ret = gpioctl_apply_snapshot_zsh(controller, line,
							  &snapshots[applied]);
		if (rollback_ret && !batch.rollback_error)
			batch.rollback_error = rollback_ret;
	}
out:
	mutex_unlock(&session->lock);
	if (copy_to_user(arg, &batch, sizeof(batch)))
		return -EFAULT;
	return ret;
}

static int gpioctl_event_config_zsh(struct gpioctl_session_zsh *session,
				    void __user *arg)
{
	struct gpioctl_controller_zsh *controller = session->controller;
	struct gpioctl_zsh_event_config config;
	struct gpioctl_line_state_zsh *line;
	unsigned long irq_flags = 0;
	int irq, ret;

	if (copy_from_user(&config, arg, sizeof(config)))
		return -EFAULT;
	ret = gpioctl_validate_header_zsh(config.abi_version,
					  config.struct_size, sizeof(config));
	if (ret)
		return ret;
	if (!gpioctl_reserved_zero_zsh(config.reserved,
					ARRAY_SIZE(config.reserved)) ||
	    config.flags || config.edge > GPIOCTL_ZSH_EDGE_BOTH)
		return -EINVAL;
	if (!controller->backend.ops->to_irq)
		return -EOPNOTSUPP;

	mutex_lock(&session->lock);
	line = gpioctl_owned_line_zsh(session, config.offset);
	if (!line) {
		ret = -EPERM;
		goto out;
	}
	gpioctl_disable_event_zsh(line);
	if (config.edge == GPIOCTL_ZSH_EDGE_NONE) {
		ret = 0;
		goto out;
	}
	ret = controller->backend.ops->direction_input(controller->backend.priv,
							line->hal_line);
	if (ret)
		goto out;
	line->output = false;
	irq = controller->backend.ops->to_irq(controller->backend.priv,
					     line->hal_line);
	if (irq < 0) {
		ret = irq;
		goto out;
	}
	if (config.edge & GPIOCTL_ZSH_EDGE_RISING)
		irq_flags |= IRQF_TRIGGER_RISING;
	if (config.edge & GPIOCTL_ZSH_EDGE_FALLING)
		irq_flags |= IRQF_TRIGGER_FALLING;
	line->edge = config.edge;
	line->debounce_us = config.debounce_us;
	line->last_event_ns = 0;
	ret = request_threaded_irq(irq, NULL, gpioctl_irq_thread_zsh,
				   irq_flags | IRQF_ONESHOT,
				   GPIOCTL_ZSH_NAME, line);
	if (ret) {
		line->edge = GPIOCTL_ZSH_EDGE_NONE;
		goto out;
	}
	line->irq = irq;
out:
	mutex_unlock(&session->lock);
	return ret;
}

static int gpioctl_iopad_config_zsh(struct gpioctl_session_zsh *session,
				    void __user *arg)
{
	struct gpioctl_controller_zsh *controller = session->controller;
	struct gpioctl_iopad_provider_zsh *provider = NULL;
	struct gpioctl_zsh_iopad_config config;
	struct gpioctl_line_state_zsh *line;
	int ret;

	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;
	if (copy_from_user(&config, arg, sizeof(config)))
		return -EFAULT;
	ret = gpioctl_validate_header_zsh(config.abi_version,
					  config.struct_size, sizeof(config));
	if (ret)
		return ret;
	if (!gpioctl_reserved_zero_zsh(config.reserved,
					ARRAY_SIZE(config.reserved)) ||
	    config.flags & ~(GPIOCTL_ZSH_IOPAD_APPLY_BIAS |
			     GPIOCTL_ZSH_IOPAD_APPLY_DRIVE |
			     GPIOCTL_ZSH_IOPAD_APPLY_MUX) ||
	    !config.flags ||
	    ((config.flags & GPIOCTL_ZSH_IOPAD_APPLY_BIAS) ?
		(config.bias < GPIOCTL_ZSH_BIAS_DISABLE ||
		 config.bias > GPIOCTL_ZSH_BIAS_PULL_DOWN) : config.bias != 0) ||
	    (!(config.flags & GPIOCTL_ZSH_IOPAD_APPLY_DRIVE) &&
		config.drive_level != 0) ||
	    ((config.flags & GPIOCTL_ZSH_IOPAD_APPLY_MUX) ?
		config.mux_state != GPIOCTL_ZSH_MUX_GPIO :
		config.mux_state != GPIOCTL_ZSH_MUX_AS_IS))
		return -EINVAL;
	if (!controller->backend.ops->set_iopad) {
		provider = gpioctl_get_iopad_zsh();
		if (!provider || !provider->desc.ops->supports(
				provider->desc.priv, controller->backend.hardware_key,
				config.offset)) {
			gpioctl_put_iopad_zsh(provider);
			return -EOPNOTSUPP;
		}
	}

	mutex_lock(&session->lock);
	line = gpioctl_owned_line_zsh(session, config.offset);
	if (!line)
		ret = -EPERM;
	else if (controller->backend.ops->set_iopad)
		ret = controller->backend.ops->set_iopad(
			controller->backend.priv, line->hal_line, &config);
	else
		ret = provider->desc.ops->configure(
			provider->desc.priv, controller->backend.hardware_key,
			config.offset, &config);
	mutex_unlock(&session->lock);
	gpioctl_put_iopad_zsh(provider);
	return ret;
}

static long gpioctl_ioctl_zsh(struct file *file, unsigned int command,
			      unsigned long argument)
{
	struct gpioctl_session_zsh *session = file->private_data;
	struct gpioctl_controller_zsh *controller = session->controller;
	void __user *arg = (void __user *)argument;
	int ret = -ENOTTY;

	switch (command) {
	case GPIOCTL_ZSH_IOC_GET_ABI: {
		struct gpioctl_zsh_abi_info info = {
			.abi_version = GPIOCTL_ZSH_ABI_VERSION,
			.struct_size = sizeof(info),
			.max_lines_per_request = GPIOCTL_ZSH_MAX_LINES,
			.max_batch_ops = GPIOCTL_ZSH_MAX_BATCH_OPS,
			.event_record_size = sizeof(struct gpioctl_zsh_event),
		};

		ret = copy_to_user(arg, &info, sizeof(info)) ? -EFAULT : 0;
		break;
	}
	case GPIOCTL_ZSH_IOC_GET_CAPS: {
		struct gpioctl_iopad_provider_zsh *provider;
		struct gpioctl_zsh_caps caps = {
			.abi_version = GPIOCTL_ZSH_ABI_VERSION,
			.struct_size = sizeof(caps),
			.controller_id = controller->id,
			.line_count = controller->backend.line_count,
			.capabilities = controller->backend.capabilities,
			.event_queue_size = GPIOCTL_ZSH_EVENT_QUEUE_SIZE,
		};

		provider = gpioctl_get_iopad_zsh();
		if (provider && provider->desc.ops->supports(
				provider->desc.priv, controller->backend.hardware_key, 0))
			caps.capabilities |= GPIOCTL_ZSH_CAP_IOPAD;
		gpioctl_put_iopad_zsh(provider);

		ret = copy_to_user(arg, &caps, sizeof(caps)) ? -EFAULT : 0;
		break;
	}
	case GPIOCTL_ZSH_IOC_GET_LINE_CAPS: {
		struct gpioctl_iopad_provider_zsh *provider;
		struct gpioctl_zsh_line_caps caps;

		if (copy_from_user(&caps, arg, sizeof(caps))) {
			ret = -EFAULT;
			break;
		}
		ret = gpioctl_validate_header_zsh(caps.abi_version,
						  caps.struct_size, sizeof(caps));
		if (ret || caps.offset >= controller->backend.line_count ||
		    !gpioctl_reserved_zero_zsh(caps.reserved,
						ARRAY_SIZE(caps.reserved))) {
			if (!ret)
				ret = -EINVAL;
			break;
		}
		caps.capabilities = controller->backend.capabilities;
		caps.drive_level_min = 0;
		caps.drive_level_max = 0;
		ret = 0;
		if (controller->backend.ops->get_iopad_caps)
			ret = controller->backend.ops->get_iopad_caps(
				controller->backend.priv, caps.offset, &caps);
		provider = gpioctl_get_iopad_zsh();
		if (!ret && provider && provider->desc.ops->supports(
				provider->desc.priv, controller->backend.hardware_key,
				caps.offset))
			ret = provider->desc.ops->get_caps(
				provider->desc.priv, controller->backend.hardware_key,
				caps.offset, &caps);
		gpioctl_put_iopad_zsh(provider);
		if (!ret && copy_to_user(arg, &caps, sizeof(caps)))
			ret = -EFAULT;
		break;
	}
	case GPIOCTL_ZSH_IOC_GET_LINE_POLICY: {
		struct gpioctl_zsh_line_policy policy;
		const struct gpioctl_line_policy_desc_zsh *source;

		if (copy_from_user(&policy, arg, sizeof(policy))) {
			ret = -EFAULT;
			break;
		}
		ret = gpioctl_validate_header_zsh(policy.abi_version,
						  policy.struct_size,
						  sizeof(policy));
		if (ret || policy.offset >= controller->backend.line_count ||
		    !gpioctl_reserved_zero_zsh(policy.reserved,
						ARRAY_SIZE(policy.reserved))) {
			if (!ret)
				ret = -EINVAL;
			break;
		}
		source = &controller->policies[policy.offset];
		policy.flags = source->flags;
		policy.safe_direction = source->safe_direction;
		policy.safe_value = source->safe_value;
		policy.safe_bias = source->safe_bias;
		if (copy_to_user(arg, &policy, sizeof(policy)))
			ret = -EFAULT;
		break;
	}
	case GPIOCTL_ZSH_IOC_LEASE_REQUEST:
		ret = gpioctl_lease_request_zsh(session, arg);
		break;
	case GPIOCTL_ZSH_IOC_LEASE_RELEASE:
		ret = gpioctl_lease_release_zsh(session, arg);
		break;
	case GPIOCTL_ZSH_IOC_LINE_CONFIG:
		ret = gpioctl_line_config_zsh(session, arg);
		break;
	case GPIOCTL_ZSH_IOC_VALUES_GET:
		ret = gpioctl_values_get_zsh(session, arg);
		break;
	case GPIOCTL_ZSH_IOC_VALUES_SET:
		ret = gpioctl_values_set_zsh(session, arg);
		break;
	case GPIOCTL_ZSH_IOC_BATCH_EXEC:
		ret = gpioctl_batch_exec_zsh(session, arg);
		break;
	case GPIOCTL_ZSH_IOC_EVENT_CONFIG:
		ret = gpioctl_event_config_zsh(session, arg);
		break;
	case GPIOCTL_ZSH_IOC_IOPAD_CONFIG:
		ret = gpioctl_iopad_config_zsh(session, arg);
		break;
	case GPIOCTL_ZSH_IOC_IOPAD_GET_CONFIG: {
		struct gpioctl_zsh_iopad_config config;

		if (copy_from_user(&config, arg, sizeof(config))) {
			ret = -EFAULT;
			break;
		}
		ret = gpioctl_validate_header_zsh(config.abi_version,
						  config.struct_size,
						  sizeof(config));
		if (ret || config.offset >= controller->backend.line_count ||
		    config.bias || config.drive_level || config.mux_state ||
		    config.flags ||
		    !gpioctl_reserved_zero_zsh(config.reserved,
						ARRAY_SIZE(config.reserved))) {
			if (!ret)
				ret = -EINVAL;
			break;
		}
		ret = gpioctl_read_iopad_zsh(controller, config.offset, &config);
		if (!ret && copy_to_user(arg, &config, sizeof(config)))
			ret = -EFAULT;
		break;
	}
	case GPIOCTL_ZSH_IOC_GET_STATS: {
		struct gpioctl_zsh_stats stats = {
			.abi_version = GPIOCTL_ZSH_ABI_VERSION,
			.struct_size = sizeof(stats),
			.operations = atomic64_read(&controller->operations),
			.errors = atomic64_read(&controller->errors),
			.denials = atomic64_read(&controller->denials),
			.lease_conflicts = atomic64_read(&controller->lease_conflicts),
			.events = atomic64_read(&controller->events),
			.event_drops = atomic64_read(&controller->event_drops),
			.active_leases = atomic_read(&controller->active_leases),
		};

		ret = copy_to_user(arg, &stats, sizeof(stats)) ? -EFAULT : 0;
		break;
	}
	default:
		break;
	}
	if (!ret)
		atomic64_inc(&controller->operations);
	else {
		atomic64_inc(&controller->errors);
		if (ret == -EPERM || ret == -EACCES)
			atomic64_inc(&controller->denials);
	}
	return ret;
}

#ifdef CONFIG_COMPAT
static long gpioctl_compat_ioctl_zsh(struct file *file, unsigned int command,
				     unsigned long argument)
{
	return gpioctl_ioctl_zsh(file, command, argument);
}
#endif

static ssize_t gpioctl_read_zsh(struct file *file, char __user *buffer,
				size_t count, loff_t *position)
{
	struct gpioctl_session_zsh *session = file->private_data;
	struct gpioctl_zsh_event event;
	unsigned long irq_flags;
	size_t copied = 0;
	int ret;

	if (count < sizeof(event))
		return -EINVAL;
	while (copied + sizeof(event) <= count) {
		spin_lock_irqsave(&session->event_lock, irq_flags);
		if (!session->event_count) {
			spin_unlock_irqrestore(&session->event_lock, irq_flags);
			if (copied)
				break;
			if (file->f_flags & O_NONBLOCK)
				return -EAGAIN;
			ret = wait_event_interruptible(session->event_wait,
				session->event_count || session->closing);
			if (ret)
				return ret;
			if (session->closing)
				return 0;
			continue;
		}
		event = session->events[session->event_tail];
		session->event_tail = (session->event_tail + 1) %
			GPIOCTL_ZSH_EVENT_QUEUE_SIZE;
		session->event_count--;
		spin_unlock_irqrestore(&session->event_lock, irq_flags);
		if (copy_to_user(buffer + copied, &event, sizeof(event)))
			return copied ? copied : -EFAULT;
		copied += sizeof(event);
	}
	return copied;
}

static __poll_t gpioctl_poll_zsh(struct file *file, poll_table *wait)
{
	struct gpioctl_session_zsh *session = file->private_data;
	__poll_t mask = 0;
	unsigned long irq_flags;

	poll_wait(file, &session->event_wait, wait);
	spin_lock_irqsave(&session->event_lock, irq_flags);
	if (session->event_count)
		mask |= EPOLLIN | EPOLLRDNORM;
	if (session->closing)
		mask |= EPOLLHUP;
	spin_unlock_irqrestore(&session->event_lock, irq_flags);
	return mask;
}

static const struct file_operations gpioctl_fops_zsh = {
	.owner = THIS_MODULE,
	.open = gpioctl_open_zsh,
	.release = gpioctl_release_zsh,
	.unlocked_ioctl = gpioctl_ioctl_zsh,
#ifdef CONFIG_COMPAT
	.compat_ioctl = gpioctl_compat_ioctl_zsh,
#endif
	.read = gpioctl_read_zsh,
	.poll = gpioctl_poll_zsh,
	.llseek = no_llseek,
};

static ssize_t backend_show(struct device *device,
			    struct device_attribute *attribute, char *buffer)
{
	struct gpioctl_controller_zsh *controller = dev_get_drvdata(device);

	return sysfs_emit(buffer, "%s\n", controller->backend.name);
}
static DEVICE_ATTR_RO(backend);

static ssize_t line_count_show(struct device *device,
			       struct device_attribute *attribute, char *buffer)
{
	struct gpioctl_controller_zsh *controller = dev_get_drvdata(device);

	return sysfs_emit(buffer, "%u\n", controller->backend.line_count);
}
static DEVICE_ATTR_RO(line_count);

#define GPIOCTL_ZSH_SYSFS_ATOMIC64(name, field) \
static ssize_t name##_show(struct device *device, \
			   struct device_attribute *attribute, char *buffer) \
{ \
	struct gpioctl_controller_zsh *controller = dev_get_drvdata(device); \
	return sysfs_emit(buffer, "%lld\n", atomic64_read(&controller->field)); \
} \
static DEVICE_ATTR_RO(name)

GPIOCTL_ZSH_SYSFS_ATOMIC64(operations, operations);
GPIOCTL_ZSH_SYSFS_ATOMIC64(errors, errors);
GPIOCTL_ZSH_SYSFS_ATOMIC64(denials, denials);
GPIOCTL_ZSH_SYSFS_ATOMIC64(lease_conflicts, lease_conflicts);
GPIOCTL_ZSH_SYSFS_ATOMIC64(events, events);
GPIOCTL_ZSH_SYSFS_ATOMIC64(event_drops, event_drops);

static ssize_t active_leases_show(struct device *device,
				  struct device_attribute *attribute,
				  char *buffer)
{
	struct gpioctl_controller_zsh *controller = dev_get_drvdata(device);

	return sysfs_emit(buffer, "%d\n", atomic_read(&controller->active_leases));
}
static DEVICE_ATTR_RO(active_leases);

#define GPIOCTL_ZSH_SYSFS_UINT(name, field) \
static ssize_t name##_show(struct device *device, \
			   struct device_attribute *attribute, char *buffer) \
{ \
	struct gpioctl_controller_zsh *controller = dev_get_drvdata(device); \
	return sysfs_emit(buffer, "%u\n", controller->field); \
} \
static DEVICE_ATTR_RO(name)

GPIOCTL_ZSH_SYSFS_UINT(allowlisted_lines, allowlisted_lines);
GPIOCTL_ZSH_SYSFS_UINT(output_lines, output_lines);
GPIOCTL_ZSH_SYSFS_UINT(reserved_lines, reserved_lines);

static struct attribute *gpioctl_zsh_attrs[] = {
	&dev_attr_backend.attr,
	&dev_attr_line_count.attr,
	&dev_attr_active_leases.attr,
	&dev_attr_allowlisted_lines.attr,
	&dev_attr_output_lines.attr,
	&dev_attr_reserved_lines.attr,
	&dev_attr_operations.attr,
	&dev_attr_errors.attr,
	&dev_attr_denials.attr,
	&dev_attr_lease_conflicts.attr,
	&dev_attr_events.attr,
	&dev_attr_event_drops.attr,
	NULL,
};
ATTRIBUTE_GROUPS(gpioctl_zsh);

static char *gpioctl_devnode_zsh(const struct device *device, umode_t *mode)
{
	if (mode)
		*mode = 0660;
	return NULL;
}

int gpioctl_register_backend_zsh(const struct gpioctl_backend_desc_zsh *desc,
				 struct gpioctl_controller_zsh **result)
{
	struct gpioctl_controller_zsh *controller;
	unsigned int i;
	int id, ret;

	if (!desc || !result || !desc->name || !desc->hardware_key ||
	    !desc->ops || !desc->owner ||
	    desc->abi_version != GPIOCTL_ZSH_HAL_ABI_VERSION ||
	    desc->struct_size != sizeof(*desc) ||
	    desc->ops->abi_version != GPIOCTL_ZSH_HAL_ABI_VERSION ||
	    desc->ops->struct_size != sizeof(*desc->ops) ||
	    !desc->line_count || desc->line_count > 256 ||
	    !desc->ops->request || !desc->ops->release ||
	    !desc->ops->direction_input || !desc->ops->direction_output ||
	    !desc->ops->get_value || !desc->ops->set_value)
		return -EINVAL;

	controller = kzalloc(sizeof(*controller), GFP_KERNEL);
	if (!controller)
		return -ENOMEM;
	controller->policies = kcalloc(desc->line_count,
				       sizeof(*controller->policies), GFP_KERNEL);
	if (!controller->policies) {
		ret = -ENOMEM;
		goto err_controller;
	}
	for (i = 0; i < desc->line_count; i++) {
		struct gpioctl_line_policy_desc_zsh *policy =
			&controller->policies[i];

		policy->safe_direction = GPIOCTL_ZSH_DIRECTION_INPUT;
		policy->safe_bias = GPIOCTL_ZSH_BIAS_DISABLE;
		if (desc->line_policies)
			*policy = desc->line_policies[i];
		ret = gpioctl_validate_policy_zsh(policy);
		if (ret)
			goto err_policies;
		if (policy->flags & GPIOCTL_ZSH_POLICY_ALLOW_UNPRIVILEGED)
			controller->allowlisted_lines++;
		if (policy->flags & GPIOCTL_ZSH_POLICY_OUTPUT_ALLOWED)
			controller->output_lines++;
		if (policy->flags & GPIOCTL_ZSH_POLICY_RESERVED)
			controller->reserved_lines++;
	}
	controller->leased = bitmap_zalloc(desc->line_count, GFP_KERNEL);
	if (!controller->leased) {
		ret = -ENOMEM;
		goto err_policies;
	}
	id = ida_alloc_max(&gpioctl_ida_zsh, GPIOCTL_ZSH_MAX_CONTROLLERS - 1,
			   GFP_KERNEL);
	if (id < 0) {
		ret = id;
		goto err_bitmap;
	}
	controller->id = id;
	controller->devt = MKDEV(MAJOR(gpioctl_base_devt_zsh), id);
	controller->backend = *desc;
	controller->backend.line_policies = controller->policies;
	mutex_init(&controller->lock);
	cdev_init(&controller->cdev, &gpioctl_fops_zsh);
	controller->cdev.owner = THIS_MODULE;
	ret = cdev_add(&controller->cdev, controller->devt, 1);
	if (ret)
		goto err_ida;
	controller->device = device_create_with_groups(gpioctl_class_zsh, NULL,
		controller->devt, controller, gpioctl_zsh_groups,
		"gpio%u_zsh", controller->id);
	if (IS_ERR(controller->device)) {
		ret = PTR_ERR(controller->device);
		goto err_cdev;
	}
	*result = controller;
	pr_info(GPIOCTL_ZSH_NAME ": registered /dev/gpio%u_zsh backend=%s lines=%u\n",
		controller->id, controller->backend.name,
		controller->backend.line_count);
	return 0;

err_cdev:
	cdev_del(&controller->cdev);
err_ida:
	ida_free(&gpioctl_ida_zsh, id);
err_bitmap:
	bitmap_free(controller->leased);
err_policies:
	kfree(controller->policies);
err_controller:
	kfree(controller);
	return ret;
}
EXPORT_SYMBOL_GPL(gpioctl_register_backend_zsh);

int gpioctl_unregister_backend_zsh(struct gpioctl_controller_zsh *controller)
{
	if (!controller)
		return -EINVAL;
	mutex_lock(&controller->lock);
	if (atomic_read(&controller->open_count) ||
	    atomic_read(&controller->active_leases)) {
		mutex_unlock(&controller->lock);
		return -EBUSY;
	}
	controller->unregistering = true;
	mutex_unlock(&controller->lock);

	device_destroy(gpioctl_class_zsh, controller->devt);
	cdev_del(&controller->cdev);
	ida_free(&gpioctl_ida_zsh, controller->id);
	bitmap_free(controller->leased);
	kfree(controller->policies);
	pr_info(GPIOCTL_ZSH_NAME ": unregistered controller=%u backend=%s\n",
		controller->id, controller->backend.name);
	kfree(controller);
	return 0;
}
EXPORT_SYMBOL_GPL(gpioctl_unregister_backend_zsh);

int gpioctl_register_iopad_provider_zsh(
	const struct gpioctl_iopad_provider_desc_zsh *desc,
	struct gpioctl_iopad_provider_zsh **result)
{
	struct gpioctl_iopad_provider_zsh *provider;
	int ret = 0;

	if (!desc || !result || !desc->name || !desc->ops || !desc->owner ||
	    desc->abi_version != GPIOCTL_ZSH_HAL_ABI_VERSION ||
	    desc->struct_size != sizeof(*desc) ||
	    desc->ops->abi_version != GPIOCTL_ZSH_HAL_ABI_VERSION ||
	    desc->ops->struct_size != sizeof(*desc->ops) ||
	    !desc->ops->supports || !desc->ops->get_caps ||
	    !desc->ops->get_config ||
	    !desc->ops->configure ||
	    (!!desc->ops->lease_prepare != !!desc->ops->lease_restore))
		return -EINVAL;
	provider = kzalloc(sizeof(*provider), GFP_KERNEL);
	if (!provider)
		return -ENOMEM;
	provider->desc = *desc;
	init_waitqueue_head(&provider->wait);
	mutex_lock(&gpioctl_iopad_lock_zsh);
	if (gpioctl_iopad_provider_zsh)
		ret = -EBUSY;
	else
		gpioctl_iopad_provider_zsh = provider;
	mutex_unlock(&gpioctl_iopad_lock_zsh);
	if (ret) {
		kfree(provider);
		return ret;
	}
	*result = provider;
	pr_info(GPIOCTL_ZSH_NAME ": registered IOPAD provider=%s\n", desc->name);
	return 0;
}
EXPORT_SYMBOL_GPL(gpioctl_register_iopad_provider_zsh);

int gpioctl_unregister_iopad_provider_zsh(
	struct gpioctl_iopad_provider_zsh *provider)
{
	if (!provider)
		return -EINVAL;
	mutex_lock(&gpioctl_iopad_lock_zsh);
	if (gpioctl_iopad_provider_zsh != provider) {
		mutex_unlock(&gpioctl_iopad_lock_zsh);
		return -ENOENT;
	}
	gpioctl_iopad_provider_zsh = NULL;
	provider->unregistering = true;
	mutex_unlock(&gpioctl_iopad_lock_zsh);
	wait_event(provider->wait, !atomic_read(&provider->active_calls));
	pr_info(GPIOCTL_ZSH_NAME ": unregistered IOPAD provider=%s\n",
		provider->desc.name);
	kfree(provider);
	return 0;
}
EXPORT_SYMBOL_GPL(gpioctl_unregister_iopad_provider_zsh);

static int __init gpioctl_core_init_zsh(void)
{
	int ret;

	ret = alloc_chrdev_region(&gpioctl_base_devt_zsh, 0,
				  GPIOCTL_ZSH_MAX_CONTROLLERS,
				  GPIOCTL_ZSH_NAME);
	if (ret)
		return ret;
	gpioctl_class_zsh = class_create("gpioctl_zsh");
	if (IS_ERR(gpioctl_class_zsh)) {
		ret = PTR_ERR(gpioctl_class_zsh);
		unregister_chrdev_region(gpioctl_base_devt_zsh,
					 GPIOCTL_ZSH_MAX_CONTROLLERS);
		return ret;
	}
	gpioctl_class_zsh->devnode = gpioctl_devnode_zsh;
	pr_info(GPIOCTL_ZSH_NAME ": loaded ABI=%u major=%u\n",
		GPIOCTL_ZSH_ABI_VERSION, MAJOR(gpioctl_base_devt_zsh));
	return 0;
}

static void __exit gpioctl_core_exit_zsh(void)
{
	class_destroy(gpioctl_class_zsh);
	unregister_chrdev_region(gpioctl_base_devt_zsh,
				 GPIOCTL_ZSH_MAX_CONTROLLERS);
	ida_destroy(&gpioctl_ida_zsh);
	pr_info(GPIOCTL_ZSH_NAME ": unloaded\n");
}

module_init(gpioctl_core_init_zsh);
module_exit(gpioctl_core_exit_zsh);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("zsh");
MODULE_DESCRIPTION("Versioned generic GPIO character-device core");
MODULE_VERSION("0.1.0");
