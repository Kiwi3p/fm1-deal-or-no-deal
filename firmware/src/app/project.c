/* SPDX-License-Identifier: GPL-3.0-only */
#include "dond.c" /* application is built as one translation unit */
project_t proj;

/* Phase 1: no mid-game persistence. Defaults only; OBJ_PROJ unused. */
int project_load(void)
{
    unsigned i;
    for (i = 0; i < P_NPARAMS; i++)
        proj.par[i] = om_param_info(i)->def;
    dond_init(plat_ms() ^ 0xD0ADu);
    return 0;
}
void project_apply(void)
{
    int i;
    for (i = 0; i < P_NPARAMS; i++)
        om_set(i, proj.par[i]);
}
