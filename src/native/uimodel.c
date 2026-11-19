#include "uimodel.h"
#include "layout.h"

void sel_devices_from_registry(Registry* r, SelDevice* out, int* count) {
    int number, i, n = 0;
    for (number = 1; number <= REG_MAX + 1; ++number) {
        for (i = 0; i < REG_MAX; ++i) {
            RegEntry* e = &r->e[i];
            if (!e->used || e->number != number || n >= SEL_MAX_DEVICES) continue;
            out[n].uid = e->uid;
            out[n].number = e->number;
            out[n].nav = e->nav;
            out[n].pad = e->pad;
            out[n].vid = e->info.vid;
            out[n].pid = e->info.pid;
            cstr_copy(out[n].key, (int)sizeof(out[n].key), e->info.key);
            ++n;
        }
    }
    *count = n;
}

void uimodel_build(UiModel* m, const Selector* s, Registry* r, const char* subtitle, const char* footer) {
    SelDevice devs[SEL_MAX_DEVICES];
    int count = 0, i, side;
    int humans = sel_local_humans(s);
    int slots_full = humans <= 1
        ? (s->side_uid[0] >= 0)
        : (s->side_uid[0] >= 0 && s->side_uid[1] >= 0);
    memset(m, 0, sizeof(*m));
    cstr_copy(m->subtitle, (int)sizeof(m->subtitle), subtitle);
    cstr_copy(m->footer, (int)sizeof(m->footer), footer);
    cstr_copy(m->message, (int)sizeof(m->message), s->message);
    m->state = s->state;
    m->local_humans = humans;
    m->layout_first = s->cfg.layout_first;
    for (side = 0; side < 2; ++side) {
        RegEntry* e = reg_by_uid(r, s->side_uid[side]);
        m->side_state[side] = sel_side_state(s, side);
        m->side_number[side] = s->side_number[side];
        m->side_layout[side] = sel_side_layout(s, side);
        m->side_cal_step[side] = sel_side_cal_step(s, side);
        m->side_held[side] = 0;
        m->side_act[side] = LAYOUT_ACT_NONE;
        if (e) {
            int cal[LAYOUT_POS_COUNT];
            const int* calp = layout_fill_cal(e->info.key, e->info.vid, e->info.pid, cal) ? cal : 0;
            int pos;
            int layout = m->side_layout[side];
            reg_label(e, m->side_label[side], (int)sizeof(m->side_label[side]));
            for (pos = 0; pos < LAYOUT_POS_COUNT; ++pos)
                if (layout_pos_held_in(&e->pad, pos, calp, layout)) m->side_held[side] |= 1u << pos;
            m->side_act[side] = layout_action_from_pad(layout, &e->pad, calp);
        }
    }
    sel_devices_from_registry(r, devs, &count);
    for (i = 0; i < count && i < UI_MAX_DEVICES; ++i) {
        RegEntry* e = reg_by_uid(r, devs[i].uid);
        UiDevice* d = &m->devs[i];
        d->number = devs[i].number;
        d->side = sel_uid_side(s, devs[i].uid);
        d->blocked = d->side < 0 && (slots_full || s->state == S_LOCKED || s->state == S_PLAYING);
        d->layout_id = sel_dev_layout(s, devs[i].uid);
        d->layout_ready = sel_dev_layout_ready(s, devs[i].uid);
        if (e) reg_label(e, d->label, (int)sizeof(d->label));
    }
    m->dev_count = count;
}
