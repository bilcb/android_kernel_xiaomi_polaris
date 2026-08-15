#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include "elliptic_device.h"
#include "elliptic_sysfs.h"
#include <elliptic/elliptic_mixer_controls.h>


#define ELLIPTIC_DIAGNOSTICS_DATA_SECTION_COUNT 16
#define ELLIPTIC_CALIBRATION_MAX_DISPLAY_COUNT  96
#define ELLIPTIC_ML_DISPLAY_COUNT 16

static int kobject_create_and_add_failed;
static int sysfs_create_group_failed;

extern struct elliptic_system_configuration_parameters_cache
	elliptic_system_configuration_cache;

static ssize_t calibration_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count) {

	ssize_t result;

	struct elliptic_shared_data_block *calibration_obj =
		elliptic_get_shared_obj(ELLIPTIC_OBJ_ID_CALIBRATION_DATA);

	if (calibration_obj == NULL) {
		EL_PRINT_E("calibration_obj is NULL");
		return -EINVAL;
	}

	if (count > calibration_obj->size) {
		EL_PRINT_E("write length %zu larger than buffer", count);
		return -EINVAL;
	}

	memcpy(calibration_obj->buffer, buf, count);
	result = (ssize_t)count;
	return result;
}

static ssize_t calibration_v2_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count) {

	ssize_t result;

	struct elliptic_shared_data_block *calibration_obj =
		elliptic_get_shared_obj(ELLIPTIC_OBJ_ID_CALIBRATION_V2_DATA);

	if (calibration_obj == NULL) {
		EL_PRINT_E("calibration_obj is NULL");
		return -EINVAL;
	}

	if (count > calibration_obj->size) {
		EL_PRINT_E("write length %zu larger than buffer", count);
		return -EINVAL;
	}

	memcpy(calibration_obj->buffer, buf, count);
	result = (ssize_t)count;
	return result;
}

static ssize_t diagnostics_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count) {

	ssize_t result;

	struct elliptic_shared_data_block *diagnostics_obj =
		elliptic_get_shared_obj(ELLIPTIC_OBJ_ID_DIAGNOSTICS_DATA);

	if (diagnostics_obj == NULL) {
		EL_PRINT_E("diagnostics_obj is NULL");
		return -EINVAL;
	}

	if (count > diagnostics_obj->size) {
		EL_PRINT_E("write length %zu larger than buffer", count);
		return -EINVAL;
	}

	memcpy(diagnostics_obj->buffer, buf, count);
	result = (ssize_t)count;
	return result;
}

static ssize_t ml_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count) {

	ssize_t result;

	struct elliptic_shared_data_block *ml_obj =
		elliptic_get_shared_obj(ELLIPTIC_OBJ_ID_ML_DATA);

	if (ml_obj == NULL) {
		EL_PRINT_E("ml_obj is NULL");
		return -EINVAL;
	}

	if (count > ml_obj->size) {
		EL_PRINT_E("write length %zu larger than buffer", count);
		return -EINVAL;
	}

	memcpy(ml_obj->buffer, buf, count);
	result = (ssize_t)count;
	return result;
}

static ssize_t calibration_show_core(struct device *dev,
	struct device_attribute *attr, char *buf, int pretty, size_t buf_size)
{
	ssize_t result;
	int length;
	int i;
	uint8_t *caldata;

	struct elliptic_shared_data_block *calibration_obj =
		elliptic_get_shared_obj(ELLIPTIC_OBJ_ID_CALIBRATION_DATA);

	if (kobject_create_and_add_failed)
		EL_PRINT_E("kobject_create_and_add_failed");

	if (sysfs_create_group_failed)
		EL_PRINT_E("sysfs_create_group_failed");

	if (calibration_obj == NULL) {
		EL_PRINT_E("calibration_obj is NULL");
		return -EINVAL;
	}

	if (calibration_obj->size > PAGE_SIZE) {
		EL_PRINT_E("calibration_obj->size > PAGE_SIZE");
		return -EINVAL;
	}

	caldata = (uint8_t *)calibration_obj->buffer;
	length = 0;
	if (pretty) {
		if (caldata[0] == 0xDE &&
			caldata[1] == 0xAD) {
			length += scnprintf(buf + length, buf_size - length,
								"Calibration Data: not loaded");
		} else {
			length += scnprintf(buf + length, buf_size - length,
								"Calibration Data: ");
			for (i = 0; i < calibration_obj->size; ++i)
				length += scnprintf(buf + length, buf_size - length,
								"0x%02x ", caldata[i]);
		}
	} else {
		for (i = 0; i < calibration_obj->size; ++i)
			length += scnprintf(buf + length, buf_size - length,
								"0x%02x ", caldata[i]);
	}
	length += scnprintf(buf + length, buf_size - length, "\n\n");
	result = (ssize_t)length;
	return result;
}

static ssize_t calibration_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return calibration_show_core(dev, attr, buf, 0, PAGE_SIZE);
}

static ssize_t calibration_v2_show_core(struct device *dev,
	struct device_attribute *attr, char *buf, int pretty, size_t buf_size)
{
	ssize_t result;
	int length;
	int i;
	uint8_t *caldata;

	struct elliptic_shared_data_block *calibration_obj =
		elliptic_get_shared_obj(ELLIPTIC_OBJ_ID_CALIBRATION_V2_DATA);

	if (kobject_create_and_add_failed)
		EL_PRINT_E("kobject_create_and_add_failed");

	if (sysfs_create_group_failed)
		EL_PRINT_E("sysfs_create_group_failed");

	if (calibration_obj == NULL) {
		EL_PRINT_E("calibration_obj is NULL");
		return -EINVAL;
	}

	if (calibration_obj->size > PAGE_SIZE) {
		EL_PRINT_E("calibration_obj->size > PAGE_SIZE");
		return -EINVAL;
	}

	caldata = (uint8_t *)calibration_obj->buffer;
	length = 0;
	if (pretty) {
		if (caldata[0] == 0xDE &&
			caldata[1] == 0xAD) {
			length += scnprintf(buf + length, buf_size - length,
								"Calibration Ext Data: not loaded");
		} else {
			int j = (ELLIPTIC_CALIBRATION_V2_DATA_SIZE>>2) - 1;

			length += scnprintf(buf + length, buf_size - length,
								"Calibration Ext Data: ");
			for (i = 0; i < ELLIPTIC_CALIBRATION_MAX_DISPLAY_COUNT; ++i)
				length += scnprintf(buf + length, buf_size - length,
								"0x%02x ", caldata[i]);
			length += scnprintf(buf + length, buf_size - length,
								"\nTruncated at %d",
								ELLIPTIC_CALIBRATION_MAX_DISPLAY_COUNT);
			length += scnprintf(buf + length, buf_size - length,
						"\nmisc: %u %u %u %u %u %u %u %u\n",
						caldata[j-7], caldata[j-6], caldata[j-5],
						caldata[j-4], caldata[j-3], caldata[j-2],
						caldata[j-1], caldata[j]);
		}
	} else {
		for (i = 0; i < calibration_obj->size; ++i)
			length += scnprintf(buf + length, buf_size - length,
								"0x%02x ", caldata[i]);
	}
	length += scnprintf(buf + length, buf_size - length, "\n\n");
	result = (ssize_t)length;
	return result;
}

static ssize_t calibration_v2_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return calibration_v2_show_core(dev, attr, buf, 0, PAGE_SIZE);
}

static ssize_t diagnostics_show_core(struct device *dev,
	struct device_attribute *attr, char *buf, int pretty, size_t buf_size)
{
	ssize_t result;
	int length;
	uint32_t *data32;
	int i;

	struct elliptic_shared_data_block *diagnostics_obj =
		elliptic_get_shared_obj(ELLIPTIC_OBJ_ID_DIAGNOSTICS_DATA);

	if (kobject_create_and_add_failed)
		EL_PRINT_E("kobject_create_and_add_failed");

	if (sysfs_create_group_failed)
		EL_PRINT_E("sysfs_create_group_failed");

	if (diagnostics_obj == NULL) {
		EL_PRINT_E("diagnostics_obj is NULL");
		return -EINVAL;
	}

	if (diagnostics_obj->size > PAGE_SIZE) {
		EL_PRINT_E("diagnostics_obj->size > PAGE_SIZE");
		return -EINVAL;
	}

	length = 0;
	data32 = (uint32_t *)diagnostics_obj->buffer;

	if (pretty) {
		length += scnprintf(buf + length, buf_size - length,"Diagnostics:\n  counters:\n");
		for (i = 0;i<ELLIPTIC_DIAGNOSTICS_DATA_SECTION_COUNT;i++)
			length += scnprintf(buf + length, buf_size - length, "   %u %u %u %u\n",
				data32[4*i], data32[4*i+1], data32[4*i+2], data32[4*i+3]);
	} else {
		for (i = 0; i < (diagnostics_obj->size >> 4); ++i)
			length += scnprintf(buf + length, buf_size - length, "   %u %u %u %u\n",
				data32[4*i], data32[4*i+1], data32[4*i+2], data32[4*i+3]);
	}
	length += scnprintf(buf + length, buf_size - length, "\n\n");
	result = (ssize_t)length;
	return result;
}

static ssize_t diagnostics_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return diagnostics_show_core(dev, attr, buf, 0, PAGE_SIZE);
}

static ssize_t ml_show_core(struct device *dev,
	struct device_attribute *attr, char *buf, int pretty, size_t buf_size)
{
	ssize_t result;
	int length;
	int i;
	uint32_t *mldata;

	struct elliptic_shared_data_block *ml_obj =
		elliptic_get_shared_obj(ELLIPTIC_OBJ_ID_ML_DATA);

	if (kobject_create_and_add_failed)
		EL_PRINT_E("kobject_create_and_add_failed");

	if (sysfs_create_group_failed)
		EL_PRINT_E("sysfs_create_group_failed");

	if (ml_obj == NULL) {
		EL_PRINT_E("ml_obj is NULL");
		return -EINVAL;
	}

	if (ml_obj->size > PAGE_SIZE) {
		EL_PRINT_E("ml_obj->size > PAGE_SIZE");
		return -EINVAL;
	}

	mldata = (uint32_t *)ml_obj->buffer;
	length = 0;
	if (pretty) {
		if (mldata[0] == 0x0 &&
			mldata[1] == 0x0) {
			length += scnprintf(buf + length, buf_size - length,
								"ML Data: not loaded");
		} else {
			length += scnprintf(buf + length, buf_size - length,
								"ML Data: ");
			for (i = 0; i < ELLIPTIC_ML_DISPLAY_COUNT; ++i)
				length += scnprintf(buf + length, buf_size - length,
								"0x%08x ", mldata[i]);
			length += scnprintf(buf + length, buf_size - length,
								"\nTruncated at %d",
								ELLIPTIC_ML_DISPLAY_COUNT);
		}
	} else {
		int values =  ml_obj->size >> 2;
		for (i = 0; i < values; ++i)
			length += scnprintf(buf + length, buf_size - length,
								"0x%08x ", mldata[i]);
	}
	length += scnprintf(buf + length, buf_size - length, "\n\n");
	result = (ssize_t)length;
	return result;
}

static ssize_t ml_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return ml_show_core(dev, attr, buf, 0, PAGE_SIZE);
}


static ssize_t version_show_core(struct device *dev,
	struct device_attribute *attr, char *buf, int pretty, size_t buf_size)
{
	ssize_t result;
	struct elliptic_engine_version_info *version_info;
	int length;

	struct elliptic_shared_data_block *version_obj =
		elliptic_get_shared_obj(ELLIPTIC_OBJ_ID_VERSION_INFO);

	if (kobject_create_and_add_failed)
		EL_PRINT_E("kobject_create_and_add_failed");

	if (sysfs_create_group_failed)
		EL_PRINT_E("sysfs_create_group_failed");

	if (version_obj == NULL) {
		EL_PRINT_E("version_obj is NULL");
		return -EINVAL;
	}

	if (version_obj->size > PAGE_SIZE) {
		EL_PRINT_E("version_obj->size > PAGE_SIZE");
		return -EINVAL;
	}

	version_info = (struct elliptic_engine_version_info *)
		version_obj->buffer;

	if (pretty) {
		if (version_info->major == 0xDE &&
			version_info->minor == 0xAD) {
			length = snprintf(buf, buf_size, "Version: unknown\n");
		} else {
			length = snprintf(buf, buf_size, "Version: %d.%d.%d.%d\n",
				version_info->major, version_info->minor, version_info->build,
				version_info->revision);
		}
	} else {
		length = snprintf(buf, buf_size, "%d.%d.%d.%d\n",
			version_info->major, version_info->minor, version_info->build,
			version_info->revision);
	}
	result = (ssize_t)length;
	return result;
}

static ssize_t version_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return version_show_core(dev, attr, buf, 0, PAGE_SIZE);
}


static ssize_t branch_show_core(struct device *dev,
	struct device_attribute *attr, char *buf, int pretty, size_t buf_size)
{
	int length;

	struct elliptic_shared_data_block *branch_obj =
		elliptic_get_shared_obj(ELLIPTIC_OBJ_ID_BRANCH_INFO);

	if (branch_obj == NULL) {
		EL_PRINT_E("branch_obj not found");
		return -EINVAL;
	}

	if (branch_obj->size > PAGE_SIZE) {
		EL_PRINT_E("branch_obj->size > PAGE_SIZE");
		return -EINVAL;
	}
	if (pretty){
		length = snprintf(buf, buf_size - 1, "Branch: %.*s\n",
			(int)branch_obj->size, (const char *)(branch_obj->buffer));
	} else {
		length = snprintf(buf, buf_size - 1, "%.*s\n",
			(int)branch_obj->size, (const char *)(branch_obj->buffer));
	}

	return (ssize_t)length;
}

static ssize_t branch_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return branch_show_core(dev, attr, buf, 0, PAGE_SIZE);
}

static ssize_t tag_show_core(struct device *dev,
	struct device_attribute *attr, char *buf, int pretty, size_t buf_size)
{
	int length;

	struct elliptic_shared_data_block *tag_obj =
		elliptic_get_shared_obj(ELLIPTIC_OBJ_ID_TAG_INFO);

	if (tag_obj == NULL) {
		EL_PRINT_E("tag_obj not found");
		return -EINVAL;
	}

	if (tag_obj->size > PAGE_SIZE) {
		EL_PRINT_E("tag_obj->size > PAGE_SIZE");
		return -EINVAL;
	}
	if (pretty){
		length = snprintf(buf, buf_size - 1, "Tag: %.*s\n",
			(int)tag_obj->size, (const char *)(tag_obj->buffer));
	} else {
		length = snprintf(buf, buf_size - 1, "%.*s\n",
			(int)tag_obj->size, (const char *)(tag_obj->buffer));
	}

	return (ssize_t)length;
}

static ssize_t tag_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return tag_show_core(dev, attr, buf, 0, PAGE_SIZE);
}

static ssize_t cache_show(char *buf, int pretty, size_t buf_size)
{
	struct elliptic_system_configuration_parameters_cache *cache =
				&elliptic_system_configuration_cache;

	int length;

	length = snprintf(buf, buf_size - 1, "Cache:\n");
	length += scnprintf(buf + length, buf_size - length, "    mi:%d\n", cache->microphone_index);
	length += scnprintf(buf + length, buf_size - length, "    om:%d\n", cache->operation_mode);
	length += scnprintf(buf + length, buf_size - length, "   omf:%d\n", cache->operation_mode_flags);
	length += scnprintf(buf + length, buf_size - length, "    cs:%d\n", cache->calibration_state);
	length += scnprintf(buf + length, buf_size - length, "    cp:%d\n", cache->calibration_profile);
	length += scnprintf(buf + length, buf_size - length, "    ug:%d\n", cache->ultrasound_gain);
	length += scnprintf(buf + length, buf_size - length, "    ll:%d\n", cache->log_level);
	length += scnprintf(buf + length, buf_size - length, "    es:%d\n", cache->engine_suspend);

	return (ssize_t)length;
}

static ssize_t state_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int length = 0;
	length += version_show_core(dev, attr, buf + length, 1, PAGE_SIZE - length);
	if (length < 0)
		return (ssize_t)length;
	if (length > PAGE_SIZE)
		return (ssize_t)0;
	length += branch_show_core(dev, attr, buf + length, 1, PAGE_SIZE - length);
	if (length < 0)
		return (ssize_t)length;
	if (length > PAGE_SIZE)
		return (ssize_t)0;
	length += tag_show_core(dev, attr, buf + length, 1, PAGE_SIZE - length);
	if (length < 0)
		return (ssize_t)length;
	if (length > PAGE_SIZE)
		return (ssize_t)0;
	length += calibration_show_core(dev, attr, buf + length, 1, PAGE_SIZE - length);
	if (length < 0)
		return (ssize_t)length;
	if (length > PAGE_SIZE)
		return (ssize_t)0;
	length += calibration_v2_show_core(dev, attr, buf + length, 1, PAGE_SIZE - length);
	if (length < 0)
		return (ssize_t)length;
	if (length > PAGE_SIZE)
		return (ssize_t)0;
	length += diagnostics_show_core(dev, attr, buf + length, 1, PAGE_SIZE - length);
	if (length < 0)
		return (ssize_t)length;
	if (length > PAGE_SIZE)
		return (ssize_t)0;
	length += ml_show_core(dev, attr, buf + length, 1, PAGE_SIZE - length);
	if (length < 0)
		return (ssize_t)length;
	if (length > PAGE_SIZE)
		return (ssize_t)0;
	length += cache_show(buf + length, 1, PAGE_SIZE - length);
	if (length < 0)
		return (ssize_t)length;
	if (length > PAGE_SIZE)
		return (ssize_t)0;
	return (ssize_t)length;
}

static struct device_attribute calibration_attr = __ATTR_RW(calibration);
static struct device_attribute version_attr = __ATTR_RO(version);
static struct device_attribute branch_attr = __ATTR_RO(branch);
static struct device_attribute calibration_v2_attr = __ATTR_RW(calibration_v2);
static struct device_attribute diagnostics_attr = __ATTR_RW(diagnostics);
static struct device_attribute state_attr = __ATTR_RO(state);
static struct device_attribute tag_attr = __ATTR_RO(tag);
static struct device_attribute ml_attr = __ATTR_RW(ml);

static struct attribute *elliptic_attrs[] = {
	&calibration_attr.attr,
	&version_attr.attr,
	&branch_attr.attr,
	&calibration_v2_attr.attr,
	&diagnostics_attr.attr,
	&state_attr.attr,
	&tag_attr.attr,
	&ml_attr.attr,
	NULL,
};

static struct attribute_group elliptic_attr_group = {
	.name = ELLIPTIC_SYSFS_ENGINE_FOLDER,
	.attrs = elliptic_attrs,
};

static struct kobject *elliptic_sysfs_kobj;

int elliptic_initialize_sysfs(void)
{
	int err;

	elliptic_sysfs_kobj = kobject_create_and_add(ELLIPTIC_SYSFS_ROOT_FOLDER,
		kernel_kobj->parent);

	if (!elliptic_sysfs_kobj) {
		kobject_create_and_add_failed = 1;
		EL_PRINT_E("failed to create kobj");
		return -ENOMEM;
	}

	err = sysfs_create_group(elliptic_sysfs_kobj, &elliptic_attr_group);

	if (err) {
		sysfs_create_group_failed = 1;
		EL_PRINT_E("failed to create sysfs group");
		kobject_put(elliptic_sysfs_kobj);
		return -ENOMEM;
	}

	return 0;
}

void elliptic_cleanup_sysfs(void)
{
	kobject_put(elliptic_sysfs_kobj);
}
