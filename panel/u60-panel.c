/* SPDX-License-Identifier: GPL-3.0-only
 * U60 Pro Enhanced screen UI. DRM/QPIC integration based on the public
 * amenekowo/mu5250_tweaking qpic_drm_demo (GPL-3.0).
 * See THIRD_PARTY_NOTICES.md for sources and preserved vendor licenses.
 */
#define _GNU_SOURCE
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "cJSON.h"
#include "qrcodegen.h"

#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#ifndef PANEL_PREVIEW
#include <linux/input.h>
#endif
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <math.h>
#include "panel-battery-status.h"

#ifndef PANEL_PREVIEW
#if defined(__has_include)
#  if __has_include(<libdrm/drm.h>)
#    include <libdrm/drm.h>
#    include <libdrm/drm_mode.h>
#  else
#    include <drm/drm.h>
#    include <drm/drm_mode.h>
#  endif
#else
#    include <drm/drm.h>
#    include <drm/drm_mode.h>
#endif

#ifndef DRM_FORMAT_RGB565
#ifndef fourcc_code
#define fourcc_code(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))
#endif
#define DRM_FORMAT_RGB565 fourcc_code('R', 'G', '1', '6')
#endif
#ifndef DRM_MODE_CONNECTED
#define DRM_MODE_CONNECTED 1
#endif
#ifndef DRM_CLIENT_CAP_ATOMIC
#define DRM_CLIENT_CAP_ATOMIC 3
#endif
#ifndef DRM_MODE_ATOMIC_ALLOW_MODESET
#define DRM_MODE_ATOMIC_ALLOW_MODESET 0x0100
#endif
#ifndef DRM_IOCTL_MODE_DESTROY_BLOB
#ifdef DRM_IOCTL_MODE_DESTROYPROPBLOB
#define DRM_IOCTL_MODE_DESTROY_BLOB DRM_IOCTL_MODE_DESTROYPROPBLOB
#endif
#endif
#ifndef DRM_MODE_OBJECT_PLANE
#define DRM_MODE_OBJECT_PLANE 0x53524150
#endif

#endif
#define W 320
#define H 480
#define BPP 16
#define NBUFS 2
#define HEADER_H 56
#define FOOT_H 46
#define ROW_H 52
#define FONT_PATH "/usr/ui/fonts/ZTEZhengYuan.ttf"
#define CLASH_CFG "/data/u60-clash/config.yaml"
#define HB_PATH "/tmp/u60-panel.hb"
#define LOCK_PATH "/tmp/u60-panel.lock"
#define TS_SOCK "/tmp/tailscale/tailscaled.sock"
#define TS_CLI "/data/tailscale/bin/tailscale"
#define LOG_PATH "/data/u60-panel/panel.log"
#define MAX_GROUPS 16
#define MAX_NODES 48
#define MAX_HITS 80
#define ROTATE180 1
#define DOUBLE_MS 450
#define LONG_MS 1200

static volatile sig_atomic_t g_stop;
static volatile sig_atomic_t g_flip;
static volatile sig_atomic_t g_power;
static volatile sig_atomic_t g_wake;
static volatile sig_atomic_t g_toggle;
static long g_ignore_pwr_until;
static long now_ms(void);

static void on_sig(int sig)
{
	(void)sig;
	g_stop = 1;
}
static void on_power(int sig) { (void)sig; g_power=1; }
static void on_flip(int sig)
{
	(void)sig;
	g_flip = 1;
}
static void on_wake(int sig)
{
	(void)sig;
	g_wake = 1;
}
static void on_toggle(int sig)
{
	(void)sig;
	g_toggle = 1;
}

static int g_logfd = -1;
static void logline(const char *fmt, ...)
{
	char buf[1024], line[1100];
	va_list ap;
	int n;
	struct timeval tv;
	struct tm tmv;
	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (n < 0)
		return;
	gettimeofday(&tv, NULL);
	localtime_r(&tv.tv_sec, &tmv);
	n = snprintf(line, sizeof(line), "[%02d:%02d:%02d] %s\n", tmv.tm_hour, tmv.tm_min, tmv.tm_sec, buf);
	if (g_logfd < 0)
		g_logfd = open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
	if (g_logfd >= 0)
		(void)!write(g_logfd, line, (size_t)n);
}

#ifndef PANEL_PREVIEW
static int panel_lock_pid(void)
{
	char pidbuf[32];
	int fd, n, ui_pid = 0;
	fd = open(LOCK_PATH, O_RDONLY);
	if (fd < 0)
		return 0;
	n = (int)read(fd, pidbuf, sizeof(pidbuf) - 1);
	close(fd);
	if (n <= 0)
		return 0;
	pidbuf[n] = 0;
	ui_pid = atoi(pidbuf);
	if (ui_pid > 1 && kill(ui_pid, 0) == 0)
		return ui_pid;
	return 0;
}

static int panel_is_running(void)
{
	return panel_lock_pid() > 0;
}

static long now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000L+t.tv_nsec/1000000; }
static void panel_toggle(void) {
 int pid=panel_lock_pid();
 logline("switch event=request target=%s mono_ms=%ld",pid>0?"factory":"panel",now_ms());
 if(pid>0){kill(pid,SIGTERM);return;}
 if(fork()==0){execl("/bin/sh","sh","/data/u60-panel/panel-run.sh",(char*)NULL);_exit(127);}
}
#else
static long now_ms(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000L+t.tv_nsec/1000000;}
#endif
#include "panel-power.h"
static const uint16_t COL_BG = 0x0841;      /* #07090d */
static const uint16_t COL_BG2 = 0x10a3;     /* #0e131b */
static const uint16_t COL_CARD = 0x18c3;    /* #151b25 */
static const uint16_t COL_TEXT = 0xef7d;
static const uint16_t COL_MUTED = 0x8c71;
static const uint16_t COL_ACCENT = 0x4e5f;  /* #3ee0b2 */
static const uint16_t COL_INK = 0x08a4;     /* #06281d */
static const uint16_t COL_WARN = 0xfd20;
static const uint16_t COL_DOWN = 0xf2aa;
static const uint16_t COL_ONBG = 0x1149;
static const uint16_t COL_DOT = 0x3a08;

struct drm_buf {
	uint32_t handle, pitch, fb_id;
	uint64_t size;
	uint16_t *map;
};
#ifndef PANEL_PREVIEW
struct drm_props {
	uint32_t plane_fb_id, plane_crtc_id, src_x, src_y, src_w, src_h;
	uint32_t crtc_x, crtc_y, crtc_w, crtc_h, crtc_mode_id, crtc_active, conn_crtc_id;
};
struct qpic_ctx {
	int fd, cur_buf, first_commit;
	unsigned int timed_frames;
	uint32_t conn_id, crtc_id, plane_id, mode_blob_id;
	struct drm_mode_modeinfo mode;
	struct drm_props props;
	struct drm_buf bufs[NBUFS];
};

static int drm_ioctl(int fd, unsigned long req, void *arg)
{
	int ret;
	do {
		ret = ioctl(fd, req, arg);
	} while (ret == -1 && (errno == EINTR || errno == EAGAIN));
	return ret;
}
static int drm_set_client_cap(int fd, uint64_t cap, uint64_t val)
{
	struct drm_set_client_cap arg = { .capability = cap, .value = val };
	return drm_ioctl(fd, DRM_IOCTL_SET_CLIENT_CAP, &arg);
}
static int drm_get_cap(int fd, uint64_t cap, uint64_t *val)
{
	struct drm_get_cap arg = { .capability = cap };
	if (drm_ioctl(fd, DRM_IOCTL_GET_CAP, &arg) < 0)
		return -1;
	*val = arg.value;
	return 0;
}
static int drm_find_prop_id(int fd, uint32_t obj_id, uint32_t obj_type, const char *want)
{
	struct drm_mode_obj_get_properties gprops;
	struct drm_mode_get_property gprop;
	uint32_t *ids = NULL, count = 0;
	uint64_t *vals = NULL;
	int i, ret = -1;
	for (;;) {
		memset(&gprops, 0, sizeof(gprops));
		gprops.obj_id = obj_id;
		gprops.obj_type = obj_type;
		gprops.count_props = count;
		if (count) {
			gprops.props_ptr = (uint64_t)(uintptr_t)ids;
			gprops.prop_values_ptr = (uint64_t)(uintptr_t)vals;
		}
		if (drm_ioctl(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &gprops) < 0)
			goto out;
		if (gprops.count_props <= count)
			break;
		count = gprops.count_props;
		free(ids);
		free(vals);
		ids = calloc(count, sizeof(uint32_t));
		vals = calloc(count, sizeof(uint64_t));
		if (!ids || !vals)
			goto out;
	}
	for (i = 0; i < (int)count; i++) {
		memset(&gprop, 0, sizeof(gprop));
		gprop.prop_id = ids[i];
		if (drm_ioctl(fd, DRM_IOCTL_MODE_GETPROPERTY, &gprop) < 0)
			continue;
		if (strncmp(gprop.name, want, sizeof(gprop.name)) == 0) {
			ret = (int)ids[i];
			break;
		}
	}
out:
	free(ids);
	free(vals);
	return ret;
}
static int drm_map_props(int fd, struct qpic_ctx *d)
{
	struct drm_props *p = &d->props;
	const struct { const char *name; uint32_t obj; uint32_t type; uint32_t *dst; } tbl[] = {
		{ "FB_ID", d->plane_id, DRM_MODE_OBJECT_PLANE, &p->plane_fb_id },
		{ "CRTC_ID", d->plane_id, DRM_MODE_OBJECT_PLANE, &p->plane_crtc_id },
		{ "SRC_X", d->plane_id, DRM_MODE_OBJECT_PLANE, &p->src_x },
		{ "SRC_Y", d->plane_id, DRM_MODE_OBJECT_PLANE, &p->src_y },
		{ "SRC_W", d->plane_id, DRM_MODE_OBJECT_PLANE, &p->src_w },
		{ "SRC_H", d->plane_id, DRM_MODE_OBJECT_PLANE, &p->src_h },
		{ "CRTC_X", d->plane_id, DRM_MODE_OBJECT_PLANE, &p->crtc_x },
		{ "CRTC_Y", d->plane_id, DRM_MODE_OBJECT_PLANE, &p->crtc_y },
		{ "CRTC_W", d->plane_id, DRM_MODE_OBJECT_PLANE, &p->crtc_w },
		{ "CRTC_H", d->plane_id, DRM_MODE_OBJECT_PLANE, &p->crtc_h },
		{ "MODE_ID", d->crtc_id, DRM_MODE_OBJECT_CRTC, &p->crtc_mode_id },
		{ "ACTIVE", d->crtc_id, DRM_MODE_OBJECT_CRTC, &p->crtc_active },
		{ "CRTC_ID", d->conn_id, DRM_MODE_OBJECT_CONNECTOR, &p->conn_crtc_id },
	};
	size_t i;
	for (i = 0; i < sizeof(tbl) / sizeof(tbl[0]); i++) {
		int pid = drm_find_prop_id(fd, tbl[i].obj, tbl[i].type, tbl[i].name);
		if (pid < 0)
			return -1;
		*tbl[i].dst = (uint32_t)pid;
	}
	return 0;
}
static int crtc_index_of(uint32_t *crtc_ids, uint32_t count, uint32_t crtc_id)
{
	uint32_t i;
	for (i = 0; i < count; i++)
		if (crtc_ids[i] == crtc_id)
			return (int)i;
	return -1;
}
static int drm_find_objects(struct qpic_ctx *d)
{
	struct drm_mode_card_res res;
	struct drm_mode_get_plane_res pres;
	struct drm_mode_modeinfo tmp_mode;
	uint32_t *conn_ids = NULL, *crtc_ids = NULL, *plane_ids = NULL;
	int i, j, ok = -1, crtc_idx = -1;
	memset(&res, 0, sizeof(res));
	if (drm_ioctl(d->fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0)
		return -1;
	if (!res.count_connectors || !res.count_crtcs)
		return -1;
	conn_ids = calloc(res.count_connectors, sizeof(uint32_t));
	crtc_ids = calloc(res.count_crtcs, sizeof(uint32_t));
	if (!conn_ids || !crtc_ids)
		goto out;
	res.connector_id_ptr = (uint64_t)(uintptr_t)conn_ids;
	res.crtc_id_ptr = (uint64_t)(uintptr_t)crtc_ids;
	if (res.count_encoders) {
		uint32_t *enc_ids = calloc(res.count_encoders, sizeof(uint32_t));
		if (!enc_ids)
			goto out;
		res.encoder_id_ptr = (uint64_t)(uintptr_t)enc_ids;
	}
	if (drm_ioctl(d->fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0)
		goto out;
	if (res.encoder_id_ptr)
		free((void *)(uintptr_t)res.encoder_id_ptr);
	for (i = 0; i < (int)res.count_connectors; i++) {
		struct drm_mode_get_connector c;
		struct drm_mode_modeinfo *modes = NULL;
		uint32_t *encs = NULL, prev_modes = 0;
		memset(&c, 0, sizeof(c));
		c.connector_id = conn_ids[i];
		c.count_modes = 1;
		c.modes_ptr = (uint64_t)(uintptr_t)&tmp_mode;
		if (drm_ioctl(d->fd, DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0)
			continue;
		if (c.connection != DRM_MODE_CONNECTED || !c.count_modes)
			continue;
		do {
			uint32_t *props = NULL;
			uint64_t *prop_vals = NULL;
			prev_modes = c.count_modes;
			free(modes);
			free(encs);
			modes = calloc(c.count_modes, sizeof(*modes));
			encs = calloc(c.count_encoders ? c.count_encoders : 1, sizeof(uint32_t));
			if (!modes || !encs)
				goto next_conn;
			if (c.count_props) {
				props = calloc(c.count_props, sizeof(uint32_t));
				prop_vals = calloc(c.count_props, sizeof(uint64_t));
			}
			c.modes_ptr = (uint64_t)(uintptr_t)modes;
			c.encoders_ptr = (uint64_t)(uintptr_t)encs;
			c.props_ptr = props ? (uint64_t)(uintptr_t)props : 0;
			c.prop_values_ptr = prop_vals ? (uint64_t)(uintptr_t)prop_vals : 0;
			if (drm_ioctl(d->fd, DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0) {
				free(props);
				free(prop_vals);
				goto next_conn;
			}
			free(props);
			free(prop_vals);
		} while (c.count_modes != prev_modes);
		for (j = 0; j < (int)c.count_modes; j++)
			if (modes[j].hdisplay == W && modes[j].vdisplay == H)
				break;
		if (j == (int)c.count_modes)
			j = 0;
		memcpy(&d->mode, &modes[j], sizeof(d->mode));
		d->conn_id = c.connector_id;
		{
			struct drm_mode_get_encoder enc;
			uint32_t enc_id = c.encoder_id ? c.encoder_id : (c.count_encoders ? encs[0] : 0);
			d->crtc_id = 0;
			if (enc_id) {
				memset(&enc, 0, sizeof(enc));
				enc.encoder_id = enc_id;
				if (drm_ioctl(d->fd, DRM_IOCTL_MODE_GETENCODER, &enc) == 0) {
					if (enc.crtc_id)
						d->crtc_id = enc.crtc_id;
					else {
						for (j = 0; j < (int)res.count_crtcs; j++)
							if (enc.possible_crtcs & (1u << j)) {
								d->crtc_id = crtc_ids[j];
								break;
							}
					}
				}
			}
			if (!d->crtc_id && res.count_crtcs)
				d->crtc_id = crtc_ids[0];
		}
		crtc_idx = crtc_index_of(crtc_ids, res.count_crtcs, d->crtc_id);
		if (crtc_idx < 0)
			goto next_conn;
		memset(&pres, 0, sizeof(pres));
		if (drm_ioctl(d->fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &pres) < 0 || !pres.count_planes)
			goto next_conn;
		free(plane_ids);
		plane_ids = calloc(pres.count_planes, sizeof(uint32_t));
		if (!plane_ids)
			goto next_conn;
		pres.plane_id_ptr = (uint64_t)(uintptr_t)plane_ids;
		if (drm_ioctl(d->fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &pres) < 0)
			goto next_conn;
		for (j = 0; j < (int)pres.count_planes; j++) {
			struct drm_mode_get_plane pl;
			uint32_t *formats = NULL;
			int k;
			memset(&pl, 0, sizeof(pl));
			pl.plane_id = plane_ids[j];
			if (drm_ioctl(d->fd, DRM_IOCTL_MODE_GETPLANE, &pl) < 0)
				continue;
			if (!(pl.possible_crtcs & (1u << crtc_idx)))
				continue;
			formats = calloc(pl.count_format_types ? pl.count_format_types : 1, sizeof(uint32_t));
			if (!formats)
				continue;
			pl.format_type_ptr = (uint64_t)(uintptr_t)formats;
			if (drm_ioctl(d->fd, DRM_IOCTL_MODE_GETPLANE, &pl) < 0) {
				free(formats);
				continue;
			}
			for (k = 0; k < (int)pl.count_format_types; k++) {
				if (formats[k] == DRM_FORMAT_RGB565) {
					d->plane_id = plane_ids[j];
					free(formats);
					ok = 0;
					goto out;
				}
			}
			free(formats);
		}
next_conn:
		free(modes);
		free(encs);
		d->crtc_id = 0;
	}
out:
	free(conn_ids);
	free(crtc_ids);
	free(plane_ids);
	return ok;
}
static int drm_create_mode_blob(struct qpic_ctx *d)
{
	struct drm_mode_create_blob blob;
	memset(&blob, 0, sizeof(blob));
	blob.length = sizeof(d->mode);
	blob.data = (uint64_t)(uintptr_t)&d->mode;
	if (drm_ioctl(d->fd, DRM_IOCTL_MODE_CREATEPROPBLOB, &blob) < 0)
		return -1;
	d->mode_blob_id = blob.blob_id;
	return 0;
}
static int drm_buf_create(struct qpic_ctx *d, struct drm_buf *b)
{
	struct drm_mode_create_dumb creq;
	struct drm_mode_map_dumb mreq;
	struct drm_mode_fb_cmd2 fb;
	memset(&creq, 0, sizeof(creq));
	creq.width = W;
	creq.height = H;
	creq.bpp = BPP;
	if (drm_ioctl(d->fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0)
		return -1;
	b->handle = creq.handle;
	b->pitch = creq.pitch;
	b->size = creq.size;
	memset(&fb, 0, sizeof(fb));
	fb.width = W;
	fb.height = H;
	fb.pixel_format = DRM_FORMAT_RGB565;
	fb.handles[0] = b->handle;
	fb.pitches[0] = b->pitch;
	if (drm_ioctl(d->fd, DRM_IOCTL_MODE_ADDFB2, &fb) < 0)
		return -1;
	b->fb_id = fb.fb_id;
	memset(&mreq, 0, sizeof(mreq));
	mreq.handle = b->handle;
	if (drm_ioctl(d->fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0)
		return -1;
	b->map = mmap(NULL, b->size, PROT_READ | PROT_WRITE, MAP_SHARED, d->fd, mreq.offset);
	if (b->map == MAP_FAILED) {
		b->map = NULL;
		return -1;
	}
	memset(b->map, 0, b->size);
	return 0;
}
static void drm_buf_destroy(struct qpic_ctx *d, struct drm_buf *b)
{
	(void)d;
	if (b->map && b->map != MAP_FAILED) {
		munmap(b->map, b->size);
		b->map = NULL;
	}
	if (b->fb_id) {
		drm_ioctl(d->fd, DRM_IOCTL_MODE_RMFB, &b->fb_id);
		b->fb_id = 0;
	}
	if (b->handle) {
		struct drm_mode_destroy_dumb req = { .handle = b->handle };
		drm_ioctl(d->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &req);
		b->handle = 0;
	}
}
static int drm_atomic_commit(struct qpic_ctx *d, struct drm_buf *b, uint32_t flags)
{
	struct drm_mode_atomic atomic;
	struct drm_props *p = &d->props;
	uint32_t objs[32], counts[32], props[32];
	uint64_t values[32];
	int n = 0;
#define ADD_PROP(obj, prop, val) do { objs[n]=(obj); counts[n]=1; props[n]=(prop); values[n]=(val); n++; } while (0)
	ADD_PROP(d->plane_id, p->plane_fb_id, b->fb_id);
	ADD_PROP(d->plane_id, p->plane_crtc_id, d->crtc_id);
	ADD_PROP(d->plane_id, p->src_x, 0);
	ADD_PROP(d->plane_id, p->src_y, 0);
	ADD_PROP(d->plane_id, p->src_w, (uint32_t)W << 16);
	ADD_PROP(d->plane_id, p->src_h, (uint32_t)H << 16);
	ADD_PROP(d->plane_id, p->crtc_x, 0);
	ADD_PROP(d->plane_id, p->crtc_y, 0);
	ADD_PROP(d->plane_id, p->crtc_w, W);
	ADD_PROP(d->plane_id, p->crtc_h, H);
	if (d->first_commit) {
		ADD_PROP(d->crtc_id, p->crtc_active, 1);
		ADD_PROP(d->crtc_id, p->crtc_mode_id, d->mode_blob_id);
		ADD_PROP(d->conn_id, p->conn_crtc_id, d->crtc_id);
		flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
	}
#undef ADD_PROP
	memset(&atomic, 0, sizeof(atomic));
	atomic.flags = flags;
	atomic.count_objs = (uint32_t)n;
	atomic.objs_ptr = (uint64_t)(uintptr_t)objs;
	atomic.count_props_ptr = (uint64_t)(uintptr_t)counts;
	atomic.props_ptr = (uint64_t)(uintptr_t)props;
	atomic.prop_values_ptr = (uint64_t)(uintptr_t)values;
	if (drm_ioctl(d->fd, DRM_IOCTL_MODE_ATOMIC, &atomic) < 0) {
		logline("ATOMIC: %s flags=0x%x first=%d", strerror(errno), flags, d->first_commit);
		return -1;
	}
	d->first_commit = 0;
	return 0;
}
static void drm_destroy(struct qpic_ctx *d)
{
	int i;
	if (d->mode_blob_id) {
#ifdef DRM_IOCTL_MODE_DESTROY_BLOB
		struct drm_mode_destroy_blob req = { .blob_id = d->mode_blob_id };
		drm_ioctl(d->fd, DRM_IOCTL_MODE_DESTROY_BLOB, &req);
#endif
		d->mode_blob_id = 0;
	}
	for (i = 0; i < NBUFS; i++)
		drm_buf_destroy(d, &d->bufs[i]);
	if (d->fd >= 0) {
		drm_ioctl(d->fd, DRM_IOCTL_DROP_MASTER, 0);
		close(d->fd);
		d->fd = -1;
	}
}
static int drm_init(struct qpic_ctx *d)
{
	uint64_t cap;
	int i;
	memset(d, 0, sizeof(*d));
	d->fd = -1;
	d->first_commit = 1;
	d->fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	if (d->fd < 0)
		return -1;
	if (drm_get_cap(d->fd, DRM_CAP_DUMB_BUFFER, &cap) < 0 || !cap)
		return -1;
	if (drm_set_client_cap(d->fd, DRM_CLIENT_CAP_ATOMIC, 1) < 0)
		return -1;
	drm_ioctl(d->fd, DRM_IOCTL_SET_MASTER, 0);
	if (drm_find_objects(d) < 0 || drm_map_props(d->fd, d) < 0 || drm_create_mode_blob(d) < 0)
		return -1;
	for (i = 0; i < NBUFS; i++)
		if (drm_buf_create(d, &d->bufs[i]) < 0)
			return -1;
	return 0;
}
static struct drm_buf *draw_buf(struct qpic_ctx *d) { return &d->bufs[d->cur_buf]; }
static void rotate180(struct drm_buf *b)
{
	int y, x;
	for (y = 0; y < (H + 1) / 2; y++) {
		uint16_t *a = (uint16_t *)((uint8_t *)b->map + y * b->pitch);
		uint16_t *c = (uint16_t *)((uint8_t *)b->map + (H - 1 - y) * b->pitch);
		if (y == H - 1 - y) {
			for (x = 0; x < W / 2; x++) {
				uint16_t t = a[x];
				a[x] = a[W - 1 - x];
				a[W - 1 - x] = t;
			}
			break;
		}
		for (x = 0; x < W; x++) {
			uint16_t t = a[x];
			a[x] = c[W - 1 - x];
			c[W - 1 - x] = t;
		}
	}
}
static int drm_flip(struct qpic_ctx *d, long draw_ms)
{
	long rotate_started = now_ms();
	if (ROTATE180)
		rotate180(&d->bufs[d->cur_buf]);
	long commit_started = now_ms();
	int status = drm_atomic_commit(d, &d->bufs[d->cur_buf], 0);
	long commit_done = now_ms();
	if (d->timed_frames < 4) {
		d->timed_frames++;
		logline("switch event=frame-stages frame=%u draw_ms=%ld rotate_ms=%ld commit_ms=%ld status=%d mono_ms=%ld",
			d->timed_frames, draw_ms, commit_started - rotate_started,
			commit_done - commit_started, status, commit_done);
	}
	if (status < 0)
		return -1;
	d->cur_buf ^= 1;
	return 0;
}

#else
struct qpic_ctx { int cur_buf; struct drm_buf bufs[2]; };
static struct drm_buf *draw_buf(struct qpic_ctx *d) { return &d->bufs[0]; }
static int drm_flip(struct qpic_ctx *d, long draw_ms) { (void)d; (void)draw_ms; return 0; }
#endif
static void fill(struct drm_buf *b, uint16_t c)
{
	int y, x;
	for (y = 0; y < H; y++) {
		uint16_t *row = (uint16_t *)((uint8_t *)b->map + y * b->pitch);
		for (x = 0; x < W; x++)
			row[x] = c;
	}
}
static int draw_clip_top=0, draw_clip_bottom=H;
static void fill_rect(struct drm_buf *b, int x0, int y0, int x1, int y1, uint16_t c)
{
	int x, y;
	if (x0 < 0) x0 = 0;
	if (y0 < draw_clip_top) y0 = draw_clip_top;
	if (x1 > W) x1 = W;
	if (y1 > draw_clip_bottom) y1 = draw_clip_bottom;
	for (y = y0; y < y1; y++) {
		uint16_t *row = (uint16_t *)((uint8_t *)b->map + y * b->pitch);
		for (x = x0; x < x1; x++)
			row[x] = c;
	}
}
static void fill_round(struct drm_buf *b, int x0, int y0, int x1, int y1, int r, uint16_t c)
{
	int x, y;
	if (r < 1) {
		fill_rect(b, x0, y0, x1, y1, c);
		return;
	}
	fill_rect(b, x0 + r, y0, x1 - r, y1, c);
	fill_rect(b, x0, y0 + r, x1, y1 - r, c);
	for (y = 0; y < r; y++) {
		int dx = (int)sqrtf((float)(r * r - y * y));
		fill_rect(b, x0 + r - dx, y0 + r - 1 - y, x0 + r, y0 + r - y, c);
		fill_rect(b, x1 - r, y0 + r - 1 - y, x1 - r + dx, y0 + r - y, c);
		fill_rect(b, x0 + r - dx, y1 - r + y, x0 + r, y1 - r + y + 1, c);
		fill_rect(b, x1 - r, y1 - r + y, x1 - r + dx, y1 - r + y + 1, c);
	}
	(void)x;
}
static void fill_round_border(struct drm_buf *b, int x0, int y0, int x1, int y1, int r, uint16_t fill, uint16_t border)
{
	int ir = r > 2 ? r - 2 : 0;
	fill_round(b, x0, y0, x1, y1, r, border);
	fill_round(b, x0 + 2, y0 + 2, x1 - 2, y1 - 2, ir, fill);
}
static void fill_circle(struct drm_buf *b, int cx, int cy, int rad, uint16_t c)
{
	int y, x;
	for (y = -rad; y <= rad; y++) {
		int dx, yy, x0, x1;
		uint16_t *row;
		if (rad * rad - y * y < 0)
			continue;
		dx = (int)sqrtf((float)(rad * rad - y * y));
		yy = cy + y;
		if (yy < draw_clip_top || yy >= draw_clip_bottom)
			continue;
		row = (uint16_t *)((uint8_t *)b->map + yy * b->pitch);
		x0 = cx - dx;
		x1 = cx + dx;
		if (x0 < 0)
			x0 = 0;
		if (x1 >= W)
			x1 = W - 1;
		for (x = x0; x <= x1; x++)
			row[x] = c;
	}
}

/* ---- font ---- */
static stbtt_fontinfo g_font;
static unsigned char *g_font_data;
static int g_font_ok;
static int font_load(void)
{
	const char *path = getenv("U60_FONT");
	int fd = open(path ? path : FONT_PATH, O_RDONLY | O_CLOEXEC);
	struct stat st;
	if (fd < 0)
		return -1;
	if (fstat(fd, &st) < 0 || st.st_size <= 0) {
		close(fd);
		return -1;
	}
	/* The device font is 15 MB. Map it read-only so switch startup only
	 * faults in the tables/glyphs used by the first frame. */
	g_font_data = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (g_font_data == MAP_FAILED) { g_font_data = NULL; return -1; }
	if (!stbtt_InitFont(&g_font, g_font_data, 0)) {
		munmap(g_font_data, (size_t)st.st_size);
		g_font_data = NULL;
		return -1;
	}
	g_font_ok = 1;
	return 0;
}
static uint32_t utf8_next(const char **s)
{
 const unsigned char *p=(const unsigned char *)*s;
 if(!*p)return 0;
 int bytes=p[0]<0x80?1:(p[0]&0xe0)==0xc0?2:(p[0]&0xf0)==0xe0?3:(p[0]&0xf8)==0xf0?4:0;
 if(!bytes){(*s)++;return 0x3f;}
 uint32_t c=bytes==1?p[0]:p[0]&((1u<<(7-bytes))-1);
 for(int n=1;n<bytes;n++){
  /* Check each continuation before advancing. A snprintf-truncated name or
   * malformed API label must not read past its NUL terminator. */
  if(!p[n]||(p[n]&0xc0)!=0x80){(*s)++;return 0x3f;}
  c=(c<<6)|(p[n]&0x3f);
 }
 *s+=bytes;
 if((bytes==2&&c<0x80)||(bytes==3&&c<0x800)||(bytes==4&&c<0x10000)||c>0x10ffff||(c>=0xd800&&c<=0xdfff))return 0x3f;
 return c;
}

static void blend_glyph(struct drm_buf *b, int x, int y, int gw, int gh, unsigned char *bmp, uint16_t fg)
{
	int i, j;
	int fr = (fg >> 11) & 0x1f, gg = (fg >> 5) & 0x3f, fb = fg & 0x1f;
	for (j = 0; j < gh; j++) {
		int yy = y + j;
		if (yy < draw_clip_top || yy >= draw_clip_bottom)
			continue;
		uint16_t *row = (uint16_t *)((uint8_t *)b->map + yy * b->pitch);
		for (i = 0; i < gw; i++) {
			int xx = x + i, a = bmp[j * gw + i];
			uint16_t bg;
			int br, bgc, bb, nr, ng, nb;
			if (xx < 0 || xx >= W || a == 0)
				continue;
			bg = row[xx];
			br = (bg >> 11) & 0x1f;
			bgc = (bg >> 5) & 0x3f;
			bb = bg & 0x1f;
			nr = (fr * a + br * (255 - a)) / 255;
			ng = (gg * a + bgc * (255 - a)) / 255;
			nb = (fb * a + bb * (255 - a)) / 255;
			row[xx] = (uint16_t)((nr << 11) | (ng << 5) | nb);
		}
	}
}
static void draw_text(struct drm_buf *b, int x, int y, const char *s, float px, uint16_t fg)
{
	float scale;
	int baseline;
	const char *p = s;
	if (!g_font_ok || !s)
		return;
	scale = stbtt_ScaleForPixelHeight(&g_font, px);
	{
		int as, ds, lg;
		stbtt_GetFontVMetrics(&g_font, &as, &ds, &lg);
		baseline = y + (int)(as * scale);
	}
	while (*p) {
		int ax, lsb, x0, y0, gw, gh;
		unsigned char *bmp;
		uint32_t cp = utf8_next(&p);
		if (!cp)
			break;
		stbtt_GetCodepointHMetrics(&g_font, (int)cp, &ax, &lsb);
		bmp = stbtt_GetCodepointBitmap(&g_font, scale, scale, (int)cp, &gw, &gh, &x0, &y0);
		if (bmp) {
			blend_glyph(b, x + (int)(lsb * scale) + x0, baseline + y0, gw, gh, bmp, fg);
			stbtt_FreeBitmap(bmp, NULL);
		}
		x += (int)(ax * scale);
		if (x > W)
			break;
	}
}
static int text_width(const char *s, float px)
{
	float scale, w = 0;
	const char *p = s;
	if (!g_font_ok || !s)
		return 0;
	scale = stbtt_ScaleForPixelHeight(&g_font, px);
	while (*p) {
		int ax, lsb;
		uint32_t cp = utf8_next(&p);
		if (!cp)
			break;
		stbtt_GetCodepointHMetrics(&g_font, (int)cp, &ax, &lsb);
		w += ax * scale;
	}
	return (int)w;
}
static void draw_text_center(struct drm_buf *b, int x0, int x1, int y, const char *s, float px, uint16_t fg)
{
	int tw = text_width(s, px);
	int x = x0 + (x1 - x0 - tw) / 2;
	if (x < x0)
		x = x0;
	draw_text(b, x, y, s, px, fg);
}
static void draw_text_clip(struct drm_buf *b, int x, int y, const char *s, float px, uint16_t fg, int max_w)
{
	char tmp[160];
	int n;
	if (!s || !s[0])
		return;
	if (text_width(s, px) <= max_w) {
		draw_text(b, x, y, s, px, fg);
		return;
	}
	snprintf(tmp, sizeof(tmp), "%s", s);
	while (tmp[0] && text_width(tmp, px) + text_width("...", px) > max_w) {
		n = (int)strlen(tmp);
		if (n <= 1) {
			tmp[0] = 0;
			break;
		}
		n--;
		while (n > 0 && ((unsigned char)tmp[n] & 0xc0) == 0x80)
			n--;
		tmp[n] = 0;
	}
	if (strlen(tmp) + 4 < sizeof(tmp))
		strcat(tmp, "...");
	draw_text(b, x, y, tmp, px, fg);
}

#ifndef PANEL_PREVIEW
/* ---- touch / power ---- */
struct touch_dev {
	int fd, cur_slot, swap_xy, last_x, last_y, down, tap;
	struct input_absinfo abs_x, abs_y;
};
static int map_axis(int v, const struct input_absinfo *a, int out_max)
{
	int span = a->maximum - a->minimum;
	if (span <= 0)
		return 0;
	if (v < a->minimum) v = a->minimum;
	if (v > a->maximum) v = a->maximum;
	return (v - a->minimum) * out_max / span;
}
static int open_named_input(const char *want)
{
	char path[64], name[256];
	int i, fd;
	for (i = 0; i < 8; i++) {
		snprintf(path, sizeof(path), "/dev/input/event%d", i);
		fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0)
			continue;
		memset(name, 0, sizeof(name));
		ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
		if (strcasestr(name, want))
			return fd;
		close(fd);
	}
	return -1;
}
static int touch_init(struct touch_dev *t)
{
	int xspan, yspan;
	memset(t, 0, sizeof(*t));
	t->fd = open_named_input("sitronix");
	if (t->fd < 0)
		t->fd = open("/dev/input/event3", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (t->fd < 0)
		return -1;
	ioctl(t->fd, EVIOCGRAB, 1);
	if (ioctl(t->fd, EVIOCGABS(ABS_MT_POSITION_X), &t->abs_x) < 0 ||
	    ioctl(t->fd, EVIOCGABS(ABS_MT_POSITION_Y), &t->abs_y) < 0) {
		ioctl(t->fd, EVIOCGABS(ABS_X), &t->abs_x);
		ioctl(t->fd, EVIOCGABS(ABS_Y), &t->abs_y);
	}
	xspan = t->abs_x.maximum - t->abs_x.minimum;
	yspan = t->abs_y.maximum - t->abs_y.minimum;
	t->swap_xy = (xspan > yspan && W < H) ? 1 : 0;
	return 0;
}
static void touch_poll(struct touch_dev *t)
{
	struct input_event ev;
	t->tap = 0;
	while (read(t->fd, &ev, sizeof(ev)) == sizeof(ev)) {
		if(ev.type==EV_SYN && ev.code==SYN_REPORT)break; /* process each contact frame, including fast swipes */
		if (ev.type == EV_ABS) {
            if(ev.code==ABS_MT_SLOT){t->cur_slot=ev.value;continue;}
            if(t->cur_slot!=0)continue;
			if (ev.code == ABS_MT_POSITION_X || ev.code == ABS_X)
				t->last_x = ev.value;
			if (ev.code == ABS_MT_POSITION_Y || ev.code == ABS_Y)
				t->last_y = ev.value;
			if (ev.code == ABS_MT_TRACKING_ID && ev.value < 0 && t->down) {
				t->down = 0;
				t->tap = 1;
			}
			if (ev.code == ABS_MT_TRACKING_ID && ev.value >= 0)
				t->down = 1;
		} else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
			if (ev.value == 1)
				t->down = 1;
			if (ev.value == 0 && t->down) {
				t->down = 0;
				t->tap = 1;
			}
		}
	}
}
static void touch_map(const struct touch_dev *t, int *ox, int *oy)
{
	if (t->swap_xy) {
		*ox = map_axis(t->last_y, &t->abs_y, W - 1);
		*oy = map_axis(t->last_x, &t->abs_x, H - 1);
	} else {
		*ox = map_axis(t->last_x, &t->abs_x, W - 1);
		*oy = map_axis(t->last_y, &t->abs_y, H - 1);
	}
	if (ROTATE180) {
		*ox = W - 1 - *ox;
		*oy = H - 1 - *oy;
	}
}

/* ---- http / clash ---- */
#endif
static char g_secret[128];
static int load_secret(void)
{
	char line[512];
	FILE *f = fopen(CLASH_CFG, "r");
	if (!f)
		return -1;
	while (fgets(line, sizeof(line), f)) {
		char *p = strstr(line, "secret:");
		if (!p)
			continue;
		p += 7;
		while (*p == ' ' || *p == '"' || *p == '\'')
			p++;
		snprintf(g_secret, sizeof(g_secret), "%s", p);
		p = g_secret + strlen(g_secret);
		while (p > g_secret && (p[-1] == '\n' || p[-1] == '\r' || p[-1] == '"' || p[-1] == '\'' || p[-1] == ' '))
			*--p = 0;
		fclose(f);
		return 0;
	}
	fclose(f);
	return -1;
}
static int load_provider_url(char *out, int n)
{
	char line[512];
	FILE *f = fopen(CLASH_CFG, "r");
	if (!f)
		return -1;
	while (fgets(line, sizeof(line), f)) {
		char *p = strstr(line, "url:");
		char *e;
		if (!p)
			continue;
		p += 4;
		while (*p == ' ' || *p == '"' || *p == '\'')
			p++;
		if (strncmp(p, "http", 4) != 0)
			continue;
		if (strstr(p, "gstatic") || strstr(p, "generate_204"))
			continue;
		snprintf(out, (size_t)n, "%s", p);
		e = out + strlen(out);
		while (e > out && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == '"' || e[-1] == '\'' || e[-1] == ' '))
			*--e = 0;
		fclose(f);
		return out[0] ? 0 : -1;
	}
	fclose(f);
	return -1;
}
static int http_do_to(const char *method,const char *path,const char *body,char *out,int outsz,int timeout_sec)
{
 int fd=-1,result=-1,status=0;size_t used=0,sent=0;char *req=NULL,*wire=NULL;
 struct sockaddr_in addr={0};struct timeval tv={.tv_sec=timeout_sec<2?2:timeout_sec};
 if(out && outsz>0)out[0]=0;
 fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return -1;
 setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
 addr.sin_family=AF_INET;addr.sin_port=htons(19090);addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
 if(connect(fd,(struct sockaddr *)&addr,sizeof(addr))<0)goto done;
 if(asprintf(&req,"%s %s HTTP/1.0\r\nHost: 127.0.0.1\r\nAuthorization: Bearer %s\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",method,path,g_secret,body?strlen(body):0,body?body:"")<0)goto done;
 size_t length=strlen(req);
 while(sent<length){ssize_t n=send(fd,req+sent,length-sent,0);if(n<0&&errno==EINTR)continue;if(n<=0)goto done;sent+=(size_t)n;}
 wire=malloc(524289);if(!wire)goto done;
 while(used<524288){ssize_t n=read(fd,wire+used,524288-used);if(n<0&&errno==EINTR)continue;if(n<0)goto done;if(!n)break;used+=(size_t)n;}
 if(used==524288)goto done;wire[used]=0;
 char *sep=strstr(wire,"\r\n\r\n");if(!sep || sscanf(wire,"HTTP/%*s %d",&status)!=1 || status<200 || status>=300)goto done;
 *sep=0;char *data=sep+4;size_t count=used-(size_t)(data-wire);
 if(strcasestr(wire,"Transfer-Encoding: chunked")){
  char *r=data,*w=data,*end=data+count;
  while(r<end){char *tail;unsigned long n=strtoul(r,&tail,16);if(tail==r)goto done;char *nl=strstr(tail,"\r\n");if(!nl)goto done;r=nl+2;if(!n)break;if(n>(unsigned long)(end-r)||end-r-(long)n<2)goto done;memmove(w,r,n);w+=n;r+=n;if(r[0]!='\r'||r[1]!='\n')goto done;r+=2;}
  count=(size_t)(w-data);
 }
 if(out && outsz>0){if(count>=(size_t)outsz)goto done;memcpy(out,data,count);out[count]=0;}
 result=(int)count;
 done:free(req);free(wire);close(fd);return result;
}
static int http_do(const char *method, const char *path, const char *body, char *out, int outsz)
{
	return http_do_to(method, path, body, out, outsz, 2);
}
static void urlenc(const char *in, char *out, int outsz)
{
	static const char *hex = "0123456789ABCDEF";
	int o = 0;
	while (*in && o < outsz - 4) {
		unsigned char c = (unsigned char)*in++;
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
			out[o++] = (char)c;
		else {
			out[o++] = '%';
			out[o++] = hex[c >> 4];
			out[o++] = hex[c & 15];
		}
	}
	out[o] = 0;
}

static char *popen_slurp(const char *cmd, char *out, int n)
{
	FILE *fp = popen(cmd, "r");
	int got = 0;
	if (!fp) {
		if (out && n)
			out[0] = 0;
		return out;
	}
	got = (int)fread(out, 1, (size_t)n - 1, fp);
	out[got < 0 ? 0 : got] = 0;
	pclose(fp);
	return out;
}

static const char *jstr(cJSON *o, const char *k)
{
	cJSON *v = cJSON_GetObjectItem(o, k);
	return (cJSON_IsString(v) && v->valuestring) ? v->valuestring : "";
}
static int ubus_json(const char *obj,const char *method,const char *args,char *out,int n)
{
 char cmd[512]; if(strchr(obj,'\'') || strchr(method,'\'') || (args && strchr(args,'\'')))return 0;
 snprintf(cmd,sizeof(cmd),"ubus -t 2 call %s %s '%s' 2>/dev/null",obj,method,args&&args[0]?args:"{}");
 FILE *fp=popen(cmd,"r");if(!fp){out[0]=0;return 0;}
 size_t got=fread(out,1,(size_t)n-1,fp);out[got]=0;int rc=pclose(fp);
 if(rc!=0 || got==(size_t)n-1)return 0;
 cJSON *j=cJSON_Parse(out);if(!j)return 0;
 cJSON *e=cJSON_GetObjectItem(j,"error_code");int ok=!e || (cJSON_IsNumber(e)&&e->valueint==0) || (cJSON_IsString(e)&&!strcmp(e->valuestring,"0"));
 cJSON_Delete(j);return ok;
}

/* ---- app state ---- */
#include "panel-shell-model.h"
enum { PAGE_CLASH = 0, PAGE_MORE };
enum {
	SEC_MENU = 0, SEC_DEV, SEC_CELL, SEC_DATA, SEC_SIM, SEC_ROUTER,
	SEC_USB, SEC_POWER, SEC_WIFI, SEC_BAND, SEC_DIAG, SEC_TS, SEC_POLICY, SEC_COUNT
};
struct group {
	char name[64];
	char now[80];
	char all[MAX_NODES][80];
	int nall;
};
struct app {
 struct panel_shell shell;
 enum panel_battery_power battery_power;
	int page, group_i, node_off, pinging, more_sec, more_off, confirm_reboot;
	int blank_sec, blanked, bl_saved;
	int detail_off, ndetail, pending_action, clash_online;
	int web_open,web_selected,web_off;
	char lan_address[48];
	time_t confirm_until;
	struct { char label[48], value[120]; } detail[64];
	char mode[16];
	char toast[80];
	time_t toast_until, last_refresh, last_full;
	int bat, temp, delay_ms, signalbar;
	char node[80];
	char ts_state[32];
	char ts_ip[40];
	char ts_url[160];
	char usb_mode[24];
	char wan[32];
	char net_type[24];
	char band[16];
	char operator[32];
	char net_select[24];
	char dns[40];
	char fw_on[8], nat_on[8], upnp_on[8], dmz_on[8];
	char sim_state[24];
	char wifi_on[8];
	char saver[8], fastboot[8];
	char fwver[48];
	char google[48];
	int google_ms, checking_google, bl;
	int quota_ok;
	double quota_remain_gb, quota_total_gb;
	time_t quota_expire, quota_fetched;
	struct group groups[MAX_GROUPS];
	int ngroups;
	char pick_group[MAX_NODES][64];
	char pick_name[MAX_NODES][80];
	int npick;
	struct { int x0, y0, x1, y1, id; } hits[MAX_HITS];
	int nhits;
};

static int find_group(const struct app *a, const char *name)
{
	int i;
	for (i = 0; i < a->ngroups; i++)
		if (strcmp(a->groups[i].name, name) == 0)
			return i;
	return -1;
}
static void resolve_leaf(struct app *a)
{
	const char *p;
	int gi, n = 0;
	gi = find_group(a, "选择节点");
	if (gi < 0)
		gi = find_group(a, "默认代理");
	if (gi < 0 && a->ngroups)
		gi = 0;
	if (gi < 0)
		return;
	p = a->groups[gi].now;
	while (p && p[0] && n++ < 8) {
		int nxt = find_group(a, p);
		if (nxt < 0) {
			snprintf(a->node, sizeof(a->node), "%s", p);
			return;
		}
		p = a->groups[nxt].now;
	}
	if (p)
		snprintf(a->node, sizeof(a->node), "%s", p);
}

static int pick_has(const struct app *a, const char *name)
{
	int i;
	for (i = 0; i < a->npick; i++)
		if (strcmp(a->pick_name[i], name) == 0)
			return 1;
	return 0;
}
static void pick_add(struct app *a, const char *group, const char *name)
{
	if (!name || !name[0] || a->npick >= MAX_NODES)
		return;
	if (!strcmp(name, "REJECT") || !strcmp(name, "REJECT-DROP") || !strcmp(name, "PASS") || !strcmp(name, "COMPATIBLE"))
		return;
	if (pick_has(a, name))
		return;
	if (find_group(a, name) >= 0)
		return;
	snprintf(a->pick_group[a->npick], sizeof(a->pick_group[0]), "%s", group);
	snprintf(a->pick_name[a->npick], sizeof(a->pick_name[0]), "%s", name);
	a->npick++;
}

static void build_picks(struct app *a)
{
	int gi, i, k;
	struct group *g;
	a->npick = 0;
	gi = find_group(a, "选择节点");
	if (gi < 0)
		gi = a->group_i;
	if (gi >= 0 && gi < a->ngroups) {
		g = &a->groups[gi];
		for (i = 0; i < g->nall; i++) {
			int nested = find_group(a, g->all[i]);
			if (nested >= 0) {
				for (k = 0; k < a->groups[nested].nall; k++)
					pick_add(a, a->groups[nested].name, a->groups[nested].all[k]);
			} else
				pick_add(a, g->name, g->all[i]);
		}
	}
	for (i = 0; i < a->ngroups; i++) {
		for (k = 0; k < a->groups[i].nall; k++)
			pick_add(a, a->groups[i].name, a->groups[i].all[k]);
	}
	gi = find_group(a, "默认代理");
	if (gi >= 0)
		pick_add(a, a->groups[gi].name, "DIRECT");
}

static void toast(struct app *a, const char *s)
{
	snprintf(a->toast, sizeof(a->toast), "%s", s);
	a->toast_until = time(NULL) + 2;
}
static void hb(void)
{
	char b[16];
	int fd = open(HB_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return;
	snprintf(b, sizeof(b), "%ld\n", now_ms()/1000);
	(void)!write(fd, b, strlen(b));
	close(fd);
}
static void hit_reset(struct app *a) { a->nhits = 0; }
static void hit_add(struct app *a, int x0, int y0, int x1, int y1, int id)
{
	if(y0<draw_clip_top)y0=draw_clip_top;
 if(y1>draw_clip_bottom)y1=draw_clip_bottom;
 if(y1<=y0||x1<=x0)return;
	if (a->nhits >= MAX_HITS)
		return;
	a->hits[a->nhits].x0 = x0;
	a->hits[a->nhits].y0 = y0;
	a->hits[a->nhits].x1 = x1;
	a->hits[a->nhits].y1 = y1;
	a->hits[a->nhits].id = id;
	a->nhits++;
}
static int hit_find(struct app *a, int x, int y)
{
	int i;
	for (i = 0; i < a->nhits; i++)
		if (x >= a->hits[i].x0 && x < a->hits[i].x1 && y >= a->hits[i].y0 && y < a->hits[i].y1)
			return a->hits[i].id;
	return -1;
}

static void refresh_clash(struct app *a)
{
	static char buf[262144];
	cJSON *root, *proxies, *it;
	int nhttp;
	if (http_do("GET", "/configs", NULL, buf, sizeof(buf)) > 0) {
		root = cJSON_Parse(buf);
		if (root) {
			cJSON *m = cJSON_GetObjectItem(root, "mode");
			if (cJSON_IsString(m) && m->valuestring)
				snprintf(a->mode, sizeof(a->mode), "%s", m->valuestring);
			cJSON_Delete(root);
		}
	}
	nhttp = http_do("GET", "/proxies", NULL, buf, sizeof(buf));
	a->last_refresh = time(NULL);
	a->clash_online = nhttp > 0;
	if (nhttp <= 0) {
		logline("clash proxies http=%d", nhttp);
		return;
	}
	root = cJSON_Parse(buf);
	if (!root) {
		logline("clash proxies json parse fail n=%d head=%.16s", nhttp, buf);
		return;
	}
	proxies = cJSON_GetObjectItem(root, "proxies");
	a->ngroups = 0;
	a->node[0] = 0;
	cJSON_ArrayForEach(it, proxies) {
		cJSON *type = cJSON_GetObjectItem(it, "type");
		cJSON *now, *all, *nm;
		struct group *g;
		if (!cJSON_IsString(type) || strcmp(type->valuestring, "Selector") != 0)
			continue;
		if (a->ngroups >= MAX_GROUPS)
			break;
		g = &a->groups[a->ngroups];
		memset(g, 0, sizeof(*g));
		snprintf(g->name, sizeof(g->name), "%s", it->string ? it->string : "");
		now = cJSON_GetObjectItem(it, "now");
		if (cJSON_IsString(now) && now->valuestring)
			snprintf(g->now, sizeof(g->now), "%s", now->valuestring);
		all = cJSON_GetObjectItem(it, "all");
		g->nall = 0;
		cJSON_ArrayForEach(nm, all) {
			if (!cJSON_IsString(nm) || g->nall >= MAX_NODES)
				continue;
			snprintf(g->all[g->nall++], sizeof(g->all[0]), "%s", nm->valuestring);
		}
		a->ngroups++;
	}
	resolve_leaf(a);
	if(a->group_i>=a->ngroups)a->group_i=0;
	build_picks(a);
	logline("clash mode=%s node=%s groups=%d picks=%d", a->mode, a->node, a->ngroups, a->npick);
	cJSON_Delete(root);
	a->last_refresh = time(NULL);
}
static void refresh_device(struct app *a, int full)
{
	char buf[2048];
	cJSON *j;
	popen_slurp("ubus call zwrt_bsp.battery list", buf, sizeof(buf));
	j = cJSON_Parse(buf);
	if (j) {
		cJSON *c = cJSON_GetObjectItem(j, "battery_capacity");
		cJSON *t = cJSON_GetObjectItem(j, "battery_temperature");
		if (cJSON_IsNumber(c))
			a->bat = c->valueint;
		if (cJSON_IsNumber(t))
			a->temp = t->valueint;
		cJSON_Delete(j);
	}
	if (ubus_json("zte_nwinfo_api", "nwinfo_get_netinfo", "{}", buf, sizeof(buf))) {
		j = cJSON_Parse(buf);
		if (j) {
			snprintf(a->net_type, sizeof(a->net_type), "%s", jstr(j, "network_type"));
			snprintf(a->band, sizeof(a->band), "%s", jstr(j, "nr5g_action_band")[0] ? jstr(j, "nr5g_action_band") : jstr(j, "wan_active_band"));
			snprintf(a->operator, sizeof(a->operator), "%s", jstr(j, "network_provider_fullname")[0] ? jstr(j, "network_provider_fullname") : jstr(j, "network_provider"));
			snprintf(a->net_select, sizeof(a->net_select), "%s", jstr(j, "net_select"));
			a->signalbar = atoi(jstr(j, "signalbar"));
			cJSON_Delete(j);
		}
	}
	{
		char br[16];
		int fd = open("/sys/class/leds/led:lcd/brightness", O_RDONLY);
		if (fd >= 0) {
			int n = (int)read(fd, br, sizeof(br) - 1);
			close(fd);
			if (n > 0) {
				br[n] = 0;
				a->bl = atoi(br);
			}
		}
	}
	if (!full)
		return;
    a->fw_on[0]=a->nat_on[0]=a->upnp_on[0]=a->dmz_on[0]=0;
    a->saver[0]=a->fastboot[0]=a->wifi_on[0]=0;
	popen_slurp("ubus call zwrt_bsp.thermal get_cpu_temp", buf, sizeof(buf));
	j = cJSON_Parse(buf);
	if (j) {
		cJSON *t = cJSON_GetObjectItem(j, "cpuss_temp");
		if (cJSON_IsNumber(t))
			a->temp = t->valueint;
		cJSON_Delete(j);
	}
	popen_slurp("ubus call zwrt_bsp.usb list", buf, sizeof(buf));
	j = cJSON_Parse(buf);
	if (j) {
		cJSON *m = cJSON_GetObjectItem(j, "mode");
		if (cJSON_IsString(m) && m->valuestring)
			snprintf(a->usb_mode, sizeof(a->usb_mode), "%s", m->valuestring);
		cJSON_Delete(j);
	}
	popen_slurp("ubus call zwrt_router.api router_get_status_no_auth", buf, sizeof(buf));
	j = cJSON_Parse(buf);
	if (j) {
		const char *m = jstr(j, "current_wan_status");
		if (m[0])
			snprintf(a->wan, sizeof(a->wan), "%s", m);
		cJSON_Delete(j);
	}
	if (ubus_json("zwrt_router.api", "router_get_dns_para", "{}", buf, sizeof(buf))) {
		j = cJSON_Parse(buf);
		if (j) {
			snprintf(a->dns, sizeof(a->dns), "%s", jstr(j, "wan_dns_mode"));
			cJSON_Delete(j);
		}
	}
	if (ubus_json("zwrt_router.api", "router_get_firewall_para", "{}", buf, sizeof(buf))) {
		j = cJSON_Parse(buf);
		if (j) {
			snprintf(a->fw_on, sizeof(a->fw_on), "%s", jstr(j, "firewall_enable"));
			snprintf(a->nat_on, sizeof(a->nat_on), "%s", jstr(j, "nat_enable"));
			snprintf(a->dmz_on, sizeof(a->dmz_on), "%s", jstr(j, "dmz_enable"));
			cJSON_Delete(j);
		}
	}
	if (ubus_json("zwrt_router.api", "router_get_upnp", "{}", buf, sizeof(buf))) {
		j = cJSON_Parse(buf);
		if (j) {
			snprintf(a->upnp_on, sizeof(a->upnp_on), "%s", jstr(j, "enable_upnp")[0] ? jstr(j, "enable_upnp") : jstr(j, "enabled"));
			cJSON_Delete(j);
		}
	}
	if (ubus_json("zwrt_zte_mdm.api", "get_sim_info", "{}", buf, sizeof(buf))) {
		j = cJSON_Parse(buf);
		if (j) {
			snprintf(a->sim_state, sizeof(a->sim_state), "%s", jstr(j, "sim_states"));
			cJSON_Delete(j);
		}
	}
	if (ubus_json("zwrt_wlan", "report", "{}", buf, sizeof(buf))) {
		j = cJSON_Parse(buf);
		if (j) {
			snprintf(a->wifi_on, sizeof(a->wifi_on), "%s", jstr(j, "wifi_onoff"));
			cJSON_Delete(j);
		}
	}
	if (ubus_json("zwrt_mc.device.manager", "get_device_info",
		      "{\"deviceInfoList\":[\"power_saver_mode\",\"quicken_power_on\"]}", buf, sizeof(buf))) {
		j = cJSON_Parse(buf);
		if (j) {
			snprintf(a->saver, sizeof(a->saver), "%s", jstr(j, "power_saver_mode"));
			snprintf(a->fastboot, sizeof(a->fastboot), "%s", jstr(j, "quicken_power_on"));
			cJSON_Delete(j);
		}
	}
	if (ubus_json("zwrt_zte_mdm.api", "get_zwrt_common_info", "{}", buf, sizeof(buf))) {
		j = cJSON_Parse(buf);
		if (j) {
			snprintf(a->fwver, sizeof(a->fwver), "%s", jstr(j, "wa_inner_version"));
			cJSON_Delete(j);
		}
	}
    a->lan_address[0]=0;
    if(ubus_json("zwrt_router.api","router_get_dhcp_router","{}",buf,sizeof(buf))){
      j=cJSON_Parse(buf);const char *ip=jstr(j,"lan_addr");struct in_addr v;
      if(inet_pton(AF_INET,ip,&v)==1)snprintf(a->lan_address,sizeof(a->lan_address),"%s",ip);
      cJSON_Delete(j);
    }
	a->last_full = time(NULL);
}
static void refresh_ts(struct app *a)
{
	char buf[8192], cmd[256];
	cJSON *j;
	a->ts_state[0] = 0;
	a->ts_ip[0] = 0;
	if (access(TS_CLI, X_OK) != 0) {
		snprintf(a->ts_state, sizeof(a->ts_state), "未安装");
		return;
	}
	snprintf(cmd, sizeof(cmd), "%s --socket=%s status --json --peers=false 2>/dev/null", TS_CLI, TS_SOCK);
	popen_slurp(cmd, buf, sizeof(buf));
	j = cJSON_Parse(buf);
	if (!j) {
		snprintf(a->ts_state, sizeof(a->ts_state), "未运行");
		return;
	}
	{
		cJSON *st = cJSON_GetObjectItem(j, "BackendState");
		cJSON *ips = cJSON_GetObjectItem(j, "TailscaleIPs");
		cJSON *url = cJSON_GetObjectItem(j, "AuthURL");
		if (cJSON_IsString(st) && st->valuestring)
			snprintf(a->ts_state, sizeof(a->ts_state), "%s", st->valuestring);
		if (cJSON_IsArray(ips) && cJSON_GetArraySize(ips) > 0) {
			cJSON *ip0 = cJSON_GetArrayItem(ips, 0);
			if (cJSON_IsString(ip0) && ip0->valuestring)
				snprintf(a->ts_ip, sizeof(a->ts_ip), "%s", ip0->valuestring);
		}
		if (cJSON_IsString(url) && url->valuestring)
			snprintf(a->ts_url, sizeof(a->ts_url), "%s", url->valuestring);
	}
	cJSON_Delete(j);
}

static int clash_select(struct app *a, const char *group, const char *name)
{
 char path[512], encg[400], resp[256];
 cJSON *j=cJSON_CreateObject(); cJSON_AddStringToObject(j,"name",name);
 char *body=cJSON_PrintUnformatted(j); cJSON_Delete(j);
 if(!body)return -1;
 urlenc(group,encg,sizeof(encg));snprintf(path,sizeof(path),"/proxies/%s",encg);
 int n=http_do("PUT",path,body,resp,sizeof(resp));free(body);
 if(n<0)return -1;
 refresh_clash(a);int i=find_group(a,group);
 return a->clash_online && i>=0 && !strcmp(a->groups[i].now,name)?0:-1;
}
static int persist_clash_mode(const char *mode)
{
	char cmd[192];
	if (strcmp(mode, "rule") && strcmp(mode, "global") && strcmp(mode, "direct"))
		return -1;
	snprintf(cmd, sizeof(cmd),
		 "sed -i 's/^mode: .*/mode: %s/' /data/u60-clash/config.yaml && echo %s > /data/u60-clash/mode",
		 mode, mode);
	if (system(cmd) != 0)
		return -1;
	logline("persist mode=%s", mode);
	return 0;
}
static int clash_mode(struct app *a, const char *mode)
{
	char body[64], resp[256];
	snprintf(body, sizeof(body), "{\"mode\":\"%s\"}", mode);
	if (http_do("PATCH", "/configs", body, resp, sizeof(resp)) < 0)
		return -1;
	http_do("DELETE", "/connections", NULL, resp, sizeof(resp));
	persist_clash_mode(mode);
	refresh_clash(a);
	return a->clash_online && !strcmp(a->mode, mode) ? 0 : -1;
}
static const char *probe_group(const struct app *a)
{
	if (find_group(a, "选择节点") >= 0)
		return "选择节点";
	if (a->pick_group[0][0])
		return a->pick_group[0];
	if (a->node[0])
		return a->node;
	return "选择节点";
}

static int delay_once(const char *group, const char *url_enc, int timeout_ms)
{
	char path[384], enc[200], resp[256];
	cJSON *j;
	int ms = -1, n;
	urlenc(group, enc, sizeof(enc));
	snprintf(path, sizeof(path), "/proxies/%s/delay?url=%s&timeout=%d", enc, url_enc, timeout_ms);
	n = http_do_to("GET", path, NULL, resp, sizeof(resp), timeout_ms / 1000 + 3);
	if (n <= 0)
		return -1;
	j = cJSON_Parse(resp);
	if (j) {
		cJSON *d = cJSON_GetObjectItem(j, "delay");
		if (cJSON_IsNumber(d) && d->valueint > 0)
			ms = d->valueint;
		cJSON_Delete(j);
	}
	return ms;
}

static void line_probe(struct app *a)
{
	static const char *urls[] = {
		"http%3A%2F%2Fwww.gstatic.com%2Fgenerate_204",
		"http%3A%2F%2Fcp.cloudflare.com%2Fgenerate_204",
		"https%3A%2F%2Fwww.gstatic.com%2Fgenerate_204",
	};
	const char *group = probe_group(a);
	int i, ms = -1;
	for (i = 0; i < 3 && ms <= 0; i++)
		ms = delay_once(group, urls[i], 8000);
	a->delay_ms = ms;
	a->google_ms = ms;
	if (ms > 0)
		snprintf(a->google, sizeof(a->google), "专线通 %d ms", ms);
	else
		snprintf(a->google, sizeof(a->google), "专线不通");
	logline("probe %s %d ms", group, ms);
}

static void clash_delay(struct app *a, const char *name)
{
	(void)name;
	line_probe(a);
}

static void google_check(struct app *a)
{
	line_probe(a);
}

static unsigned long long userinfo_ull(const char *s, const char *key)
{
	char pat[24];
	const char *p;
	snprintf(pat, sizeof(pat), "%s=", key);
	p = strstr(s, pat);
	if (!p)
		return 0;
	return strtoull(p + strlen(pat), NULL, 10);
}

static void refresh_quota(struct app *a)
{
	char url[320], cmd[400], hdr[4096], info[512];
	const char *p, *nl;
	unsigned long long down, total, remain;
	int i;
	a->quota_fetched = time(NULL);
	if (load_provider_url(url, sizeof(url)) < 0) {
		logline("quota: no provider url");
		return;
	}
	snprintf(cmd, sizeof(cmd),
		 "curl -skD - -o /dev/null --connect-timeout 6 --max-time 12 '%s' 2>/dev/null", url);
	popen_slurp(cmd, hdr, sizeof(hdr));
	p = hdr;
	info[0] = 0;
	while (*p) {
		if (!strncasecmp(p, "subscription-userinfo:", 22)) {
			p += 22;
			while (*p == ' ' || *p == '\t')
				p++;
			nl = strchr(p, '\n');
			snprintf(info, sizeof(info), "%.*s", nl ? (int)(nl - p) : (int)strlen(p), p);
			break;
		}
		nl = strchr(p, '\n');
		p = nl ? nl + 1 : p + strlen(p);
	}
	if (!info[0]) {
		logline("quota: no userinfo header");
		return;
	}
	for (i = 0; info[i]; i++)
		if (info[i] >= 'A' && info[i] <= 'Z')
			info[i] = (char)(info[i] - 'A' + 'a');
	down = userinfo_ull(info, "download");
	total = userinfo_ull(info, "total");
	if (total == 0) {
		logline("quota: empty total");
		return;
	}
	remain = total > down ? total - down : 0;
	a->quota_remain_gb = (double)remain / (1024.0 * 1024.0 * 1024.0);
	a->quota_total_gb = (double)total / (1024.0 * 1024.0 * 1024.0);
	a->quota_expire = (time_t)userinfo_ull(info, "expire");
	a->quota_ok = 1;
	logline("quota remain=%.1fGiB total=%.0fGiB", a->quota_remain_gb, a->quota_total_gb);
}

static void net_set(struct app *a, const char *sel)
{
	char args[80], out[256];
	snprintf(args, sizeof(args), "{\"net_select\":\"%s\"}", sel);
	if (ubus_json("zte_nwinfo_api", "nwinfo_set_netselect", args, out, sizeof(out))) {
		snprintf(a->net_select, sizeof(a->net_select), "%s", sel);
		toast(a, "已改选网");
	} else
		toast(a, "选网失败");
	refresh_device(a, 1);
    toast(a,!strcmp(a->net_select,sel)?"选网已回读确认":"选网未确认，请刷新");
}

static void clash_reload(struct app *a)
{
	int pid = 0;
	char buf[32];
	popen_slurp("pidof mihomo", buf, sizeof(buf));
	pid = atoi(buf);
	if (pid > 1) {
		toast(a, kill(pid,SIGHUP)==0 ? "已发送重载请求" : "重载请求失败");
	} else
		toast(a, "Clash 未运行");
}

#include "panel-lcd-notify.h"
static struct panel_lcd_notice screen_notice;

static int lcd_set(int v) {
 if(v<0||v>255)return 0;char b[16];int n=snprintf(b,sizeof(b),"%d",v);
 int fd=open("/sys/class/leds/led:lcd/brightness",O_WRONLY|O_CLOEXEC);if(fd<0)return 0;
 ssize_t written=write(fd,b,(size_t)n);close(fd);if(written!=n)return 0;
 FILE*f=fopen("/sys/class/leds/led:lcd/brightness","r");int actual=-1;if(f){fscanf(f,"%d",&actual);fclose(f);}return actual==v;
}
static int save_setting_int(const char *name,int value) {
 char path[160],tmp[180],b[24];snprintf(path,sizeof(path),"/data/u60-panel/%s",name);snprintf(tmp,sizeof(tmp),"%s.tmp",path);
 int fd=open(tmp,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0600);if(fd<0)return 0;int n=snprintf(b,sizeof(b),"%d\n",value);
 int ok=write(fd,b,(size_t)n)==n&&fsync(fd)==0;close(fd);if(ok)ok=rename(tmp,path)==0;if(!ok)unlink(tmp);return ok;
}
static int load_brightness(void){FILE*f=fopen("/data/u60-panel/brightness","r");int v=160;if(f){fscanf(f,"%d",&v);fclose(f);}return v>=1&&v<=255?v:160;}

static int load_blank_sec(void)
{
	char b[16];
	int fd, n, s = 300;
	fd = open("/data/u60-panel/blank_sec", O_RDONLY);
	if (fd < 0)
		return 300;
	n = (int)read(fd, b, sizeof(b) - 1);
	close(fd);
	if (n > 0) {
		b[n] = 0;
		s = atoi(b);
	}
	if (s < 0)
		s = 0;
	if (s != 0 && s != 15 && s != 30 && s != 60 && s != 300)
		s = 30;
	return s;
}

static int save_blank_sec(int s) {return save_setting_int("blank_sec",s);}

static void screen_unblank(struct app *a)
{
	int v = a->bl_saved > 0 ? a->bl_saved : (a->bl > 0 ? a->bl : 255);
	if(!lcd_set(v)){snprintf(a->shell.status,sizeof(a->shell.status),"亮屏失败，请检查显示服务");return;}
	a->bl = v;
	a->blanked = 0;
 panel_lcd_notice_request(&screen_notice,1);
}

static void screen_blank(struct app *a)
{
	if (a->blanked)
		return;
	if (a->bl > 0)
		a->bl_saved = a->bl;
	if(!lcd_set(0)){snprintf(a->shell.status,sizeof(a->shell.status),"熄屏失败，请重试");return;}
	a->blanked = 1;
 panel_lcd_notice_request(&screen_notice,0);
}

static void bl_cycle(struct app *a)
{
	int next;
	if (a->blanked)
		screen_unblank(a);
	if (a->bl < 100)
		next = 160;
	else if (a->bl < 220)
		next = 255;
	else
		next = 80;
	lcd_set(next);
	a->bl = next;
	a->bl_saved = next;
	toast(a, next >= 220 ? "亮度高" : (next >= 160 ? "亮度中" : "亮度低"));
}

static void band_reset(struct app *a)
{
	char out[256];
	int ok=ubus_json("zte_nwinfo_api", "nwinfo_reset_band_cell_setting", "{}", out, sizeof(out));
	ok=ubus_json("zte_nwinfo_api", "nwinfo_rest_band_rat", "{}", out, sizeof(out)) && ok;
	refresh_device(a, 1);
	toast(a, ok ? "已请求解除，请刷新核对" : "解除请求失败");
}

enum {
	HID_MORE = 6, HID_PING = 8, HID_BACK = 9,
	HID_NODE0 = 10, /* 10 .. 10+MAX_NODES-1 */
	HID_MODE_RULE = 80, HID_MODE_GLOBAL, HID_MODE_DIRECT,
	HID_TS_LOGIN = 90, HID_TS_DOWN,
	HID_BACK_MENU = 94, HID_SCROLL_UP, HID_SCROLL_DN, HID_NODE_UP, HID_NODE_DN,
	HID_MENU0 = 100,
	HID_NAT = 200, HID_UPNP, HID_FW, HID_SAVER, HID_FASTBOOT, HID_REBOOT,
	HID_NET_AUTO, HID_NET_5G, HID_NET_4G,
	HID_GOOGLE, HID_CLASH_RELOAD, HID_BAND_RESET, HID_BL, HID_REFRESH,
	HID_BLANK_OFF, HID_BLANK_15, HID_BLANK_30, HID_BLANK_60, HID_BLANK_300,
	HID_CONFIRM, HID_CANCEL, HID_GROUP_UP, HID_GROUP_DN,
    HID_WEB_OPEN, HID_WEB_CLOSE, HID_WEB_LIST, HID_WEB_UP, HID_WEB_DN, HID_WEB_ITEM0=500, HID_MEMBER0 = 400
};

static const char *mode_label(const char *mode)
{
	if (strcmp(mode, "rule") == 0)
		return "规则";
	if (strcmp(mode, "global") == 0)
		return "全局";
	if (strcmp(mode, "direct") == 0)
		return "直连";
	return mode[0] ? mode : "—";
}
static uint16_t delay_color(int ms)
{
	if (ms <= 0)
		return COL_MUTED;
	if (ms < 150)
		return COL_ACCENT;
	if (ms < 400)
		return COL_WARN;
	return COL_DOWN;
}

static void draw_header(struct drm_buf *b, struct app *a)
{
	fill_rect(b, 0, 0, W, 3, COL_ACCENT);
	fill_rect(b, 0, 3, W, HEADER_H, COL_BG);
	draw_text(b, 16, 12, a->page == PAGE_MORE ? "设备控制" : "Clash", 20, COL_TEXT);
	draw_text(b, 16, 36, a->page == PAGE_MORE ? "双击电源回 Clash" : (a->clash_online ? "双击电源进完整功能" : "Mihomo · 正在连接"), 11, COL_MUTED);
	fill_round(b, W - 78, 12, W - 12, 40, 12, COL_ONBG);
	draw_text_center(b, W - 78, W - 12, 18, a->page==PAGE_MORE?"网页设置":mode_label(a->mode), 12, COL_ACCENT);
    if(a->page==PAGE_MORE)hit_add(a,W-90,3,W,HEADER_H,HID_WEB_OPEN);
}

static void draw_hero(struct drm_buf *b, struct app *a)
{
	char delay[40], quota[48], shown[80];
	int y = HEADER_H + 6;
	uint16_t dc;
	fill_round(b, 12, y, W - 12, y + 108, 16, COL_CARD);
	draw_text(b, 24, y + 8, "当前节点", 12, COL_MUTED);
	snprintf(shown, sizeof(shown), "%s", a->node[0] ? a->node : "加载中");
	{
		char *cut = strstr(shown, "-");
		if (cut && strstr(cut, "GB"))
			*cut = 0;
	}
	draw_text_clip(b, 24, y + 24, shown, 20, COL_TEXT, W - 52);
	if (a->quota_ok)
		snprintf(quota, sizeof(quota), "剩余 %.1f GiB / %.0f GiB", a->quota_remain_gb, a->quota_total_gb);
	else
		snprintf(quota, sizeof(quota), "订阅用量暂不可用");
	draw_text_clip(b, 24, y + 48, quota, 13, a->quota_ok ? COL_ACCENT : COL_MUTED, W - 52);
	if (a->pinging)
		snprintf(delay, sizeof(delay), "测速中");
	else if (a->delay_ms > 0)
		snprintf(delay, sizeof(delay), "专线 %d ms", a->delay_ms);
	else if (a->delay_ms < 0)
		snprintf(delay, sizeof(delay), "专线不通");
	else
		snprintf(delay, sizeof(delay), "尚未测速");
	dc = a->pinging ? COL_WARN : delay_color(a->delay_ms);
	draw_text(b, 24, y + 72, delay, 13, dc);
	fill_round(b, W - 102, y + 68, W - 24, y + 98, 12, COL_ACCENT);
	draw_text_center(b, W - 102, W - 24, y + 76, a->pinging ? "..." : "测速", 14, COL_INK);
	hit_add(a, W - 102, y + 68, W - 24, y + 98, HID_PING);
}

static void draw_modes(struct drm_buf *b, struct app *a)
{
	const char *lab[] = { "规则", "全局", "直连" };
	const char *modes[] = { "rule", "global", "direct" };
	int y = HEADER_H + 122;
	int i, inner, x0, x1;
	fill_round(b, 12, y, W - 12, y + 44, 14, COL_BG2);
	inner = (W - 32) / 3;
	for (i = 0; i < 3; i++) {
		int on = strcmp(a->mode, modes[i]) == 0;
		x0 = 16 + i * inner;
		x1 = x0 + inner - 4;
		if (on)
			fill_round(b, x0, y + 4, x1, y + 40, 12, COL_ACCENT);
		draw_text_center(b, x0, x1, y + 14, lab[i], 15, on ? COL_INK : COL_TEXT);
		hit_add(a, x0, y + 4, x1, y + 40, HID_MODE_RULE + i);
	}
}

static void draw_nodes(struct drm_buf *b, struct app *a)
{
	int y, i, shown = 0, max_rows, list_bottom;
	list_bottom = H - FOOT_H - 8;
	y = HEADER_H + 172;
	draw_text(b, 16, y, "节点", 12, COL_MUTED);
	if (a->npick > 3) {
		fill_round(b, W - 120, y - 2, W - 68, y + 22, 8, COL_CARD);
		draw_text_center(b, W - 120, W - 68, y + 2, "上", 12, COL_TEXT);
		hit_add(a, W - 120, y - 2, W - 68, y + 22, HID_NODE_UP);
		fill_round(b, W - 62, y - 2, W - 12, y + 22, 8, COL_CARD);
		draw_text_center(b, W - 62, W - 12, y + 2, "下", 12, COL_TEXT);
		hit_add(a, W - 62, y - 2, W - 12, y + 22, HID_NODE_DN);
	}
	y += 22;
	if (!a->npick) {
		fill_round(b, 12, y, W - 12, y + 56, 14, COL_CARD);
		draw_text(b, 24, y + 18, a->ngroups ? "没有可切换节点" : "正在读取节点", 14, COL_MUTED);
		return;
	}
	if (a->node_off < 0)
		a->node_off = 0;
	max_rows = (list_bottom - y) / ROW_H;
	if (max_rows < 1)
		max_rows = 1;
	if (a->node_off > a->npick - max_rows)
		a->node_off = a->npick > max_rows ? a->npick - max_rows : 0;
	for (i = a->node_off; i < a->npick && shown < max_rows; i++) {
		int on = strcmp(a->pick_name[i], a->node) == 0 ||
			 (strcmp(a->pick_name[i], "DIRECT") == 0 && strcmp(a->mode, "direct") == 0);
		int y1 = y + ROW_H - 6;
		const char *sub;
		if (on)
			fill_round_border(b, 12, y, W - 12, y1, 14, COL_ONBG, COL_ACCENT);
		else
			fill_round(b, 12, y, W - 12, y1, 14, COL_CARD);
		if (on) {
			fill_circle(b, 28, y + 22, 9, COL_ONBG);
			fill_circle(b, 28, y + 22, 5, COL_ACCENT);
		} else {
			fill_circle(b, 28, y + 22, 5, COL_DOT);
		}
		draw_text_clip(b, 42, y + 8, a->pick_name[i], 14, COL_TEXT, W - 70);
		if (!strcmp(a->pick_name[i], "DIRECT"))
			sub = on ? "正在直连" : "不走代理";
		else
			sub = on ? "正在使用" : "轻点切换";
		draw_text(b, 42, y + 28, sub, 11, on ? COL_ACCENT : COL_MUTED);
		hit_add(a, 12, y, W - 12, y1, HID_NODE0 + i);
		y += ROW_H;
		shown++;
	}
}

static void chip(struct drm_buf *b, struct app *a, int x0, int y0, int x1, int y1, const char *t, int id, int on)
{
	fill_round(b, x0, y0, x1, y1, 12, on ? COL_ACCENT : COL_CARD);
	draw_text_center(b, x0, x1, y0 + 10, t, 14, on ? COL_INK : COL_TEXT);
	hit_add(a, x0, y0, x1, y1, id);
}

#include "panel-ui.h"
#include "panel-web-settings.h"
#include "panel-shell-ui.h"

static void draw_footer(struct drm_buf *b, struct app *a)
{
	char left[80];
	int y = H - FOOT_H;
	const char *op = a->operator[0] ? a->operator : "蜂窝";
	fill_rect(b, 0, y, W, H, COL_BG2);
	if (a->page == PAGE_MORE) {
		fill_round(b, 12, y + 8, 120, y + 38, 10, COL_ACCENT);
		draw_text_center(b, 12, 120, y + 14, a->more_sec == SEC_MENU ? "回节点" : "功能目录", 13, COL_INK);
		hit_add(a, 12, y + 2, 120, y + 44, a->more_sec == SEC_MENU ? HID_MORE : HID_BACK_MENU);
		fill_round(b, W - 86, y + 8, W - 12, y + 38, 10, COL_CARD);
		draw_text_center(b, W - 86, W - 12, y + 14, "原厂", 13, COL_TEXT);
		hit_add(a, W - 86, y + 2, W - 12, y + 44, HID_BACK);
		return;
	}
	snprintf(left, sizeof(left), "%d%%", a->bat);
	draw_text(b, 16, y + 14, left, 12, COL_MUTED);
	draw_text_clip(b, 52, y + 14, op, 12, COL_MUTED, 80);
	fill_round(b, 148, y + 8, 228, y + 38, 10, COL_CARD);
	draw_text_center(b, 148, 228, y + 14, "后台", 13, COL_TEXT);
	hit_add(a, 148, y + 2, 228, y + 44, HID_MORE);
	fill_round(b, W - 86, y + 8, W - 12, y + 38, 10, COL_CARD);
	draw_text_center(b, W - 86, W - 12, y + 14, "返回", 13, COL_TEXT);
	hit_add(a, W - 86, y + 2, W - 12, y + 44, HID_BACK);
}

static void draw_toast(struct drm_buf *b, struct app *a)
{
	int tw, x0, y0;
	if (!a->toast[0] || time(NULL) > a->toast_until)
		return;
	tw = text_width(a->toast, 13);
	if (tw > W - 48)
		tw = W - 48;
	x0 = (W - tw - 28) / 2;
	y0 = H - FOOT_H - 52;
	fill_round(b, x0, y0, x0 + tw + 28, y0 + 32, 10, COL_BG2);
	draw_text_clip(b, x0 + 14, y0 + 8, a->toast, 13, COL_TEXT, tw + 4);
}

static int render(struct qpic_ctx *d, struct app *a) {
 long draw_started=now_ms();
 struct drm_buf *b=draw_buf(d);hit_reset(a);shell_render(b,a);
 int status=drm_flip(d,now_ms()-draw_started);if(status<0)logline("flip failed");return status;
}

static void ts_login(struct app *a)
{
	char cmd[384];
	if (access(TS_CLI, X_OK) != 0) {
		toast(a, "未安装 Tailscale");
		return;
	}
	snprintf(cmd, sizeof(cmd),
		 "%s --socket=%s up --accept-dns=false --hostname=u60-pro --timeout=12s >/tmp/ts-up.out 2>&1 &",
		 TS_CLI, TS_SOCK);
	if (system(cmd) == 0) {
		toast(a, "已发起登录，请电脑端授权");
		refresh_ts(a);
	} else
		toast(a, "启动登录失败");
}
static void ts_down(struct app *a)
{
	char cmd[256];
	snprintf(cmd, sizeof(cmd), "%s --socket=%s down >/dev/null 2>&1", TS_CLI, TS_SOCK);
	int ok=system(cmd)==0;
	refresh_ts(a);
    toast(a,ok && strcmp(a->ts_state,"Running") ? "断开请求已执行" : "断开未确认");
}

static void handle_hit(struct app *a, int id)
{
 if(id==HID_WEB_OPEN){a->web_open=1;a->web_selected=-1;a->web_off=0;return;}
 if(id==HID_WEB_CLOSE){a->web_open=0;return;}
 if(id==HID_WEB_LIST){a->web_selected=-1;return;}
 if(id>=HID_WEB_ITEM0 && id<HID_WEB_ITEM0+(int)(sizeof(web_settings)/sizeof(web_settings[0]))){a->web_selected=id-HID_WEB_ITEM0;return;}
 if(id==HID_WEB_UP || id==HID_WEB_DN){int indices[64],n=web_matches(a,indices);int off=a->web_off+(id==HID_WEB_UP?-5:5);if(off>=0&&off<n)a->web_off=off;return;}

 if(id==HID_CANCEL){a->pending_action=0;return;}
 if(id==HID_CONFIRM){if(!a->pending_action || time(NULL)>a->confirm_until){a->pending_action=0;return;}id=a->pending_action;a->pending_action=0;}
 else if(id==HID_NAT||id==HID_UPNP||id==HID_FW||id==HID_SAVER||id==HID_FASTBOOT||id==HID_REBOOT||id==HID_NET_AUTO||id==HID_NET_5G||id==HID_NET_4G||id==HID_BAND_RESET||id==HID_TS_DOWN){a->pending_action=id;a->confirm_until=time(NULL)+30;return;}
 if(id==HID_BACK_MENU){a->more_sec=SEC_MENU;a->pending_action=0;return;}
 if(id>=HID_MENU0 && id<HID_MENU0+SEC_COUNT){section_open(a,id-HID_MENU0);return;}
 if(id==HID_REFRESH){refresh_device(a,1);refresh_detail(a);toast(a,"状态已刷新");return;}
 if(id==HID_GROUP_UP || id==HID_GROUP_DN){if(a->ngroups)a->group_i=(a->group_i+a->ngroups+(id==HID_GROUP_UP?-1:1))%a->ngroups;a->detail_off=0;return;}
 if(id>=HID_MEMBER0 && id<HID_MEMBER0+MAX_NODES){int k=id-HID_MEMBER0;if(a->group_i>=0&&a->group_i<a->ngroups&&k<a->groups[a->group_i].nall){char g[64],v[80];snprintf(g,sizeof(g),"%s",a->groups[a->group_i].name);snprintf(v,sizeof(v),"%s",a->groups[a->group_i].all[k]);toast(a,clash_select(a,g,v)==0?"策略组已更新":"切换失败，请刷新");}return;}
 if(id==HID_SCROLL_UP || id==HID_SCROLL_DN){if(a->more_sec==SEC_MENU)a->more_off=id==HID_SCROLL_DN;else{int count=a->more_sec==SEC_POLICY?(a->ngroups?a->groups[a->group_i].nall:0):a->ndetail;int off=a->detail_off+(id==HID_SCROLL_UP?-4:4);if(off>=0&&off<count)a->detail_off=off;}return;}

	if (id == HID_MORE) {
		if (a->page == PAGE_MORE) {
			a->page = PAGE_CLASH;
		} else {
			a->page = PAGE_MORE;
			a->more_sec = SEC_MENU;
			refresh_device(a, 1);
			refresh_ts(a);
		}
		return;
	}
	if (id == HID_PING) {
		a->pinging = 1;
		toast(a, "正在测专线");
		return;
	}
	if (id == HID_BACK) {
		g_stop = 1;
		return;
	}
	if (id == HID_NODE_UP && a->node_off > 0) {
		a->node_off--;
		return;
	}
	if (id == HID_NODE_DN) {
		a->node_off++;
		return;
	}
	if (id >= HID_NODE0 && id < HID_NODE0 + MAX_NODES) {
		int idx = id - HID_NODE0;
		if (idx >= 0 && idx < a->npick) {
			int ok;
			if (!strcmp(a->pick_name[idx], "DIRECT")) {
				ok = clash_select(a, a->pick_group[idx], "DIRECT") == 0;
				if (ok)
					clash_mode(a, "direct");
			} else {
				ok = clash_select(a, a->pick_group[idx], a->pick_name[idx]) == 0;
				if (ok && strcmp(a->pick_group[idx], "选择节点") == 0)
					clash_select(a, "默认代理", "选择节点");
				if (ok && !strcmp(a->mode, "direct"))
					clash_mode(a, "rule");
			}
			toast(a, ok ? "已切换节点" : "切换失败");
		}
		return;
	}
	if (id == HID_MODE_RULE)
		toast(a, clash_mode(a, "rule")==0 ? "已改为规则分流" : "模式切换失败");
	else if (id == HID_MODE_GLOBAL)
		toast(a, clash_mode(a, "global")==0 ? "已改为全局代理" : "模式切换失败");
	else if (id == HID_MODE_DIRECT)
		toast(a, clash_mode(a, "direct")==0 ? "已改为直连" : "模式切换失败");
	else if (id == HID_TS_LOGIN)
		ts_login(a);
	else if (id == HID_TS_DOWN)
		ts_down(a);
	else if (id == HID_NET_AUTO)
		net_set(a, "WL_AND_5G");
	else if (id == HID_NET_5G)
		net_set(a, "Only_5G");
	else if (id == HID_NET_4G)
		net_set(a, "Only_LTE");
	else if (id == HID_GOOGLE) {
		a->checking_google = 1;
		toast(a, "正在测 Google");
	} else if (id == HID_CLASH_RELOAD)
		clash_reload(a);
	else if (id == HID_BAND_RESET)
		band_reset(a);
	else if (id == HID_BL)
		bl_cycle(a);
	else if (id == HID_BLANK_OFF || id == HID_BLANK_15 || id == HID_BLANK_30 ||
		 id == HID_BLANK_60 || id == HID_BLANK_300) {
		int s = 30;
		const char *lab = "30 秒";
		if (id == HID_BLANK_OFF) { s = 0; lab = "不自动熄屏"; }
		else if (id == HID_BLANK_15) { s = 15; lab = "15 秒"; }
		else if (id == HID_BLANK_30) { s = 30; lab = "30 秒"; }
		else if (id == HID_BLANK_60) { s = 60; lab = "1 分钟"; }
		else { s = 300; lab = "5 分钟"; }
		a->blank_sec = s;
		save_blank_sec(s);
		toast(a, lab);
	}
	else if (id == HID_NAT || id==HID_FW || id==HID_UPNP || id==HID_SAVER || id==HID_FASTBOOT) {
  char out[512],args[160];const char *cur=id==HID_NAT?a->nat_on:id==HID_FW?a->fw_on:id==HID_UPNP?a->upnp_on:id==HID_SAVER?a->saver:a->fastboot;
  if(strcmp(cur,"0") && strcmp(cur,"1")){toast(a,"状态未知，未执行修改");return;}
  int next=!strcmp(cur,"0"),ok;const char *obj="zwrt_router.api",*method;
  if(id==HID_SAVER||id==HID_FASTBOOT){obj="zwrt_mc.device.manager";method="set_device_info";snprintf(args,sizeof(args),"{\"deviceInfoList\":{\"%s\":\"%d\"}}",id==HID_SAVER?"power_saver_mode":"quicken_power_on",next);}
  else{method=id==HID_NAT?"router_set_nat_switch":id==HID_FW?"router_set_firewall_switch":"router_set_upnp_switch";snprintf(args,sizeof(args),"{\"%s\":%d}",id==HID_UPNP?"enable_upnp":"enable",next);}
  ok=ubus_json(obj,method,args,out,sizeof(out));refresh_device(a,1);
  toast(a,ok && cur[0]==(next?'1':'0')?"设置已回读确认":"修改未确认，请刷新");refresh_detail(a);
 } else if(id==HID_REBOOT){toast(a,"正在请求重启");if(system("reboot")!=0)toast(a,"重启请求失败");}

}

#ifndef PANEL_PREVIEW
#include "panel-worker.h"
#include "panel-touch-gate.h"
static int ui_main(void) {
 long startup=now_ms(),release_started,font_done,drm_done;int status=0, snapshot_logged=0;
 struct qpic_ctx drm;struct touch_dev touch;struct app app;memset(&app,0,sizeof(app));app.bat=-1;
 signal(SIGPIPE,SIG_IGN);signal(SIGINT,on_sig);signal(SIGTERM,on_sig);signal(SIGUSR1,on_wake);signal(SIGHUP,on_power);
 if(font_load()<0)return 1;font_done=now_ms();
 if(drm_init(&drm)<0)return 1;drm_done=now_ms();
 if(touch_init(&touch)<0){drm_destroy(&drm);return 1;}
 app.blank_sec=load_blank_sec();app.bl=load_brightness();app.bl_saved=app.bl;lcd_set(app.bl);panel_lcd_notice_init(&screen_notice,1);long last_input=now_ms(),last_sample=0,last_hb=0;
 logline("switch event=startup-stages font_ms=%ld drm_init_ms=%ld touch_lcd_ms=%ld mono_ms=%ld",font_done-startup,drm_done-font_done,last_input-drm_done,last_input);
 logline("display started: auto_blank=%d seconds",app.blank_sec);
 /* Present before the input poll or any helper fork/state collection. The
  * first snapshot fills in the placeholders asynchronously as before. */
 if(render(&drm,&app)<0){status=1;goto out;}
 logline("switch event=first-frame mono_ms=%ld startup_ms=%ld",now_ms(),now_ms()-startup);
 struct panel_touch_gate touch_gate={0};
 int need=0;while(!g_stop){
  int was_blanked=app.blanked;
  if(g_wake){g_wake=0;last_input=now_ms();if(app.blanked)screen_unblank(&app);else screen_blank(&app);need=1;}
  if(g_power){g_power=0;last_input=now_ms();screen_unblank(&app);app.shell.power_open=1;logline("power menu opened");need=1;}
  if(was_blanked&&!app.blanked)next_snapshot=0;
  /* Input and helper completion wake the loop immediately. An idle screen
   * only needs the next telemetry tick, rather than 33 wakeups per second. */
  long sample_wait=1000-(now_ms()-last_sample);if(sample_wait<0)sample_wait=0;
  int wait_ms=touch.down||screen_notice.pid?30:app.blanked?1000:(int)sample_wait;
  struct pollfd events[2]={{touch.fd,POLLIN,0},{pw.pid&&!touch.down?pw.fd:-1,POLLIN,0}};
  poll(events,2,wait_ms);struct pollfd p={touch.fd,POLLIN,0};
  /* Consume queued motion frames before drawing, avoiding a growing backlog
   * when touch sampling is faster than the LCD commit rate. Stop at release
   * so a subsequent tap sees fresh hit targets. */
  for(int frames=0;frames<64;frames++){
   touch_poll(&touch);
   int blocked=was_blanked||app.blanked||touch_gate.blocked;
   panel_touch_accept(&touch_gate,was_blanked||app.blanked,touch.down,touch.tap);
   int x,y;touch_map(&touch,&x,&y);
   if(blocked){app.shell.drag_active=0;}else{if(touch.down||touch.tap)last_input=now_ms();if(shell_pointer(&app,x,y,touch.down,touch.tap))need=1;}
   if(!touch.down||poll(&p,1,0)<=0)break;
  }
  touch.tap=0;
  int lcd_result=panel_lcd_notice_poll(&screen_notice,now_ms());
  if(lcd_result>0)logline("stock LCD notification applied: %s",screen_notice.applied?"on":"off");
  else if(lcd_result<0){logline("stock LCD notification failed; retry latest state");snprintf(app.shell.status,sizeof(app.shell.status),"屏幕唤醒通知未确认，正在重试");app.shell.status_until=now_ms()+4000;need=1;}
  if(!touch.down&&worker_poll(&app))need=1;
  if(!snapshot_logged&&cJSON_GetArraySize(cJSON_GetObjectItem(app.shell.snapshot,"sections"))>0){snapshot_logged=1;logline("switch event=first-snapshot mono_ms=%ld startup_ms=%ld",now_ms(),now_ms()-startup);}
  long t=now_ms();if(!app.blanked&&t-last_sample>=1000){sample_local(&app);last_sample=t;need=1;}
  if(t-last_hb>=5000){hb();last_hb=t;}
  if(!app.blanked&&!app.shell.power_open&&app.blank_sec>0&&t-last_input>=app.blank_sec*1000L){logline("automatic screen blank after %d seconds",app.blank_sec);screen_blank(&app);need=1;}
  if(need&&!app.blanked){render(&drm,&app);need=0;}
 }
out:
 release_started=now_ms();
 panel_lcd_notice_close(&screen_notice);
 if(pw.pid&&!pw.action)worker_clear(1);else if(pw.pid){/* Action must finish even when switching display. */close(pw.fd);free(pw.buf);}
 cJSON_Delete(app.shell.snapshot);cJSON_Delete(app.shell.draft);cJSON_Delete(app.shell.pending_args);cJSON_Delete(app.shell.report_lines);
 ioctl(touch.fd,EVIOCGRAB,0);close(touch.fd);drm_destroy(&drm);
 logline("switch event=panel-released mono_ms=%ld release_ms=%ld",now_ms(),now_ms()-release_started);return status;
}
static void watch_action(enum panel_power_action act,int active) {
 int ui=panel_lock_pid();if(act==PANEL_POWER_DOUBLE)panel_toggle();
 else if(active&&ui>0&&act==PANEL_POWER_SINGLE)kill(ui,SIGUSR1);
 else if(active&&ui>0&&act==PANEL_POWER_LONG)kill(ui,SIGHUP);
}
static int watch_main(void) {
 int fd=open_named_input("pwrkey");if(fd<0)fd=open("/dev/input/event0",O_RDONLY|O_CLOEXEC);if(fd<0)return 1;
 fcntl(fd,F_SETFL,fcntl(fd,F_GETFL,0)|O_NONBLOCK);struct panel_power_state pc;panel_power_init(&pc);int grabbed=0;
 signal(SIGINT,on_sig);signal(SIGTERM,on_sig);signal(SIGUSR1,on_toggle);signal(SIGCHLD,SIG_IGN);
 while(!g_stop){int active=panel_is_running();if(active!=grabbed){if(ioctl(fd,EVIOCGRAB,active)==0){grabbed=active;panel_power_init(&pc);}}
  if(g_toggle){g_toggle=0;panel_toggle();panel_power_init(&pc);}
  struct pollfd p={fd,POLLIN,0};poll(&p,1,panel_power_poll_ms(&pc));struct input_event ev;
  while(read(fd,&ev,sizeof(ev))==sizeof(ev))if(ev.type==EV_KEY&&ev.code==KEY_POWER)watch_action(panel_power_event(&pc,ev.value,now_ms()),active);
  watch_action(panel_power_tick(&pc,now_ms()),active);
 }
 ioctl(fd,EVIOCGRAB,0);close(fd);return 0;
}

int main(int argc, char **argv)
{
	if (argc > 1 && strcmp(argv[1], "watch") == 0)
		return watch_main();
	if (argc > 1 && strcmp(argv[1], "usb-macnet") == 0) {
		fputs("USB live switching is unavailable pending enumeration and recovery validation.\n", stderr);
		return 1;
	}
	return ui_main();
}

#else
static void shell_dispatch(struct app*a,const char*s,cJSON*j){(void)s;(void)j;a->shell.busy=1;}
static void shell_request_refresh(struct app*a){(void)a;}
static void shell_factory(struct app*a){(void)a;}
static void shell_power(struct app*a,int r){(void)a;(void)r;}
#include "panel-preview.h"
#endif
