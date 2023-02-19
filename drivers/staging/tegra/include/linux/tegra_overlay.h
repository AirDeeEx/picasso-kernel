/*
 * Copyright (C) 2010 NVIDIA Corporation
 * Author: Dan Willemsen <dwillemsen@nvidia.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __LINUX_TEGRA_OVERLAY_H
#define __LINUX_TEGRA_OVERLAY_H

#include <linux/ioctl.h>
#include <linux/types.h>
#include <video/tegrafb.h>

#define TEGRA_FB_WIN_BLEND_NONE		0
#define TEGRA_FB_WIN_BLEND_PREMULT	1
#define TEGRA_FB_WIN_BLEND_COVERAGE	2

#define TEGRA_FB_WIN_FLAG_INVERT_H	(1 << 0)
#define TEGRA_FB_WIN_FLAG_INVERT_V	(1 << 1)
#define TEGRA_FB_WIN_FLAG_TILED		(1 << 2)

/* set index to -1 to ignore window data */
struct tegra_overlay_windowattr {
	__s32	index;
	__u32	buff_id;
	__u32	blend;
	__u32	offset;
	__u32	offset_u;
	__u32	offset_v;
	__u32	stride;
	__u32	stride_uv;
	__u32	pixformat;
	__u32	x;
	__u32	y;
	__u32	w;
	__u32	h;
	__u32	out_x;
	__u32	out_y;
	__u32	out_w;
	__u32	out_h;
	__u32	z;
	__u32	pre_syncpt_id;
	__u32	pre_syncpt_val;
	__u32	hfilter;
	__u32	vfilter;
	__u32	do_not_use__tiled; /* compatibility */
	__u32	flags;
};

#define TEGRA_OVERLAY_FLIP_FLAG_BLEND_REORDER (1 << 0)
#define TEGRA_FB_FLIP_N_WINDOWS			3

struct tegra_overlay_flip_args {
	struct tegra_overlay_windowattr win[TEGRA_FB_FLIP_N_WINDOWS];
	__u32 post_syncpt_id;
	__u32 post_syncpt_val;
	__u32 flags;
};

struct tegra_dc_ext_lut {
	__u32  win_index; /* window index to set lut for */
	__u32  flags;     /* Flag bitmask, see TEGRA_DC_EXT_LUT_FLAGS_* */
	__u32  start;     /* start index to update lut from */
	__u32  len;       /* number of valid lut entries */
	__u16 *r;         /* array of 16-bit red values, 0 to reset */
	__u16 *g;         /* array of 16-bit green values, 0 to reset */
	__u16 *b;         /* array of 16-bit blue values, 0 to reset */
};

struct tegra_dc_dv {
	__u8 r;
	__u8 g;
	__u8 b;
	__u8 en;
};

#define TEGRA_DC_EXT_LUT_FLAGS_FBOVERRIDE 0x01

#define TEGRA_OVERLAY_IOCTL_MAGIC		'O'

#define TEGRA_OVERLAY_IOCTL_OPEN_WINDOW		_IOWR(TEGRA_OVERLAY_IOCTL_MAGIC, 0x40, __u32)
#define TEGRA_OVERLAY_IOCTL_CLOSE_WINDOW	_IOW(TEGRA_OVERLAY_IOCTL_MAGIC, 0x41, __u32)
#define TEGRA_OVERLAY_IOCTL_FLIP		_IOW(TEGRA_OVERLAY_IOCTL_MAGIC, 0x42, struct tegra_overlay_flip_args)
#define TEGRA_OVERLAY_IOCTL_SET_NVMAP_FD	_IOW(TEGRA_OVERLAY_IOCTL_MAGIC, 0x43, __u32)
#define TEGRA_OVERLAY_IOCTL_SET_LUT		_IOW(TEGRA_OVERLAY_IOCTL_MAGIC, 0x44, struct tegra_dc_ext_lut)
#define TEGRA_OVERLAY_IOCTL_SET_DV		_IOW(TEGRA_OVERLAY_IOCTL_MAGIC, 0x45, struct tegra_dc_dv)

#define TEGRA_OVERLAY_IOCTL_MIN_NR		_IOC_NR(TEGRA_OVERLAY_IOCTL_OPEN_WINDOW)
#define TEGRA_OVERLAY_IOCTL_MAX_NR		_IOC_NR(TEGRA_OVERLAY_IOCTL_SET_DV)

#endif
