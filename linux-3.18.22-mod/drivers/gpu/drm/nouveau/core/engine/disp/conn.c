/*
 * Copyright 2014 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */

#include <core/os.h>
#include <nvif/event.h>

#include <subdev/gpio.h>

#include "conn.h"
#include "outp.h"

static int
nvkm_connector_hpd(struct nvkm_notify *notify)
{
	struct nvkm_connector *conn = container_of(notify, typeof(*conn), hpd);
	struct nouveau_disp *disp = nouveau_disp(conn);
	struct nouveau_gpio *gpio = nouveau_gpio(conn);
	const struct nvkm_gpio_ntfy_rep *line = notify->data;
	struct nvif_notify_conn_rep_v0 rep;
	int index = conn->index;

	DBG("HPD: %d\n", line->mask);

	if (!gpio->get(gpio, 0, DCB_GPIO_UNUSED, conn->hpd.index))
		rep.mask = NVIF_NOTIFY_CONN_V0_UNPLUG;
	else
		rep.mask = NVIF_NOTIFY_CONN_V0_PLUG;
	rep.version = 0;

	nvkm_event_send(&disp->hpd, rep.mask, index, &rep, sizeof(rep));
	return NVKM_NOTIFY_KEEP;
}

int
_nvkm_connector_fini(struct nouveau_object *object, bool suspend)
{
	struct nvkm_connector *conn = (void *)object;
	nvkm_notify_put(&conn->hpd);
	return nouveau_object_fini(&conn->base, suspend);
}

int
_nvkm_connector_init(struct nouveau_object *object)
{
	struct nvkm_connector *conn = (void *)object;
	int ret = nouveau_object_init(&conn->base);
	if (ret == 0)
		nvkm_notify_get(&conn->hpd);
	return ret;
}

void
_nvkm_connector_dtor(struct nouveau_object *object)
{
	struct nvkm_connector *conn = (void *)object;
	nvkm_notify_fini(&conn->hpd);
	nouveau_object_destroy(&conn->base);
}

int
nvkm_connector_create_(struct nouveau_object *parent,
		       struct nouveau_object *engine,
		       struct nouveau_oclass *oclass,
		       struct nvbios_connE *info, int index,
		       int length, void **pobject)
{
	static const u8 hpd[] = { 0x07, 0x08, 0x51, 0x52, 0x5e, 0x5f, 0x60 };
	struct nouveau_gpio *gpio = nouveau_gpio(parent);
	struct nouveau_disp *disp = (void *)engine;
	struct nvkm_connector *conn;
	struct nvkm_output *outp;
	struct dcb_gpio_func func;
	int ret;

	list_for_each_entry(outp, &disp->outp, head) {
		if (outp->conn && outp->conn->index == index) {
			atomic_inc(&nv_object(outp->conn)->refcount);
			*pobject = outp->conn;
			return 1;
		}
	}

	ret = nouveau_object_create_(parent, engine, oclass, 0, length, pobject);
	conn = *pobject;
	if (ret)
		return ret;

	conn->info = *info;
	conn->index = index;

	DBG("type %02x loc %d hpd %02x dp %x di %x sr %x lcdid %x\n",
	    info->type, info->location, info->hpd, info->dp,
	    info->di, info->sr, info->lcdid);

	if ((info->hpd = ffs(info->hpd))) {
		if (--info->hpd >= ARRAY_SIZE(hpd)) {
			ERR("hpd %02x unknown\n", info->hpd);
			return 0;
		}
		info->hpd = hpd[info->hpd];

		ret = gpio->find(gpio, 0, info->hpd, DCB_GPIO_UNUSED, &func);
		if (ret) {
			ERR("func %02x lookup failed, %d\n", info->hpd, ret);
			return 0;
		}

		ret = nvkm_notify_init(NULL, &gpio->event, nvkm_connector_hpd,
				       true, &(struct nvkm_gpio_ntfy_req) {
					.mask = NVKM_GPIO_TOGGLED,
					.line = func.line,
				       },
				       sizeof(struct nvkm_gpio_ntfy_req),
				       sizeof(struct nvkm_gpio_ntfy_rep),
				       &conn->hpd);
		if (ret) {
			ERR("func %02x failed, %d\n", info->hpd, ret);
		} else {
			DBG("func %02x (HPD)\n", info->hpd);
		}
	}

	return 0;
}

int
_nvkm_connector_ctor(struct nouveau_object *parent,
		     struct nouveau_object *engine,
		     struct nouveau_oclass *oclass, void *info, u32 index,
		     struct nouveau_object **pobject)
{
	struct nvkm_connector *conn;
	int ret;

	ret = nvkm_connector_create(parent, engine, oclass, info, index, &conn);
	*pobject = nv_object(conn);
	if (ret)
		return ret;

	return 0;
}

struct nouveau_oclass *
nvkm_connector_oclass = &(struct nvkm_connector_impl) {
	.base = {
		.handle = 0,
		.ofuncs = &(struct nouveau_ofuncs) {
			.ctor = _nvkm_connector_ctor,
			.dtor = _nvkm_connector_dtor,
			.init = _nvkm_connector_init,
			.fini = _nvkm_connector_fini,
		},
	},
}.base;
