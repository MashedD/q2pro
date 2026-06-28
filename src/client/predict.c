/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "client.h"

typedef enum {
    STEP_SMOOTH_Q2PRO,
    STEP_SMOOTH_R1Q2_1,
    STEP_SMOOTH_R1Q2_2,
    STEP_SMOOTH_R1Q2_3
} stepSmoothMode_t;

static unsigned r1q2_step_frame;

static stepSmoothMode_t CL_StepSmoothMode(void)
{
    const char *s = cl_step_smoothing_mode->string;

    if (!Q_stricmp(s, "r1q2-1") || !Q_stricmp(s, "r1q2_1") ||
        !Q_stricmp(s, "r1q2 1")) {
        return STEP_SMOOTH_R1Q2_1;
    }
    if (!Q_stricmp(s, "r1q2-2") || !Q_stricmp(s, "r1q2_2") ||
        !Q_stricmp(s, "r1q2 2")) {
        return STEP_SMOOTH_R1Q2_2;
    }
    if (!Q_stricmp(s, "r1q2-3") || !Q_stricmp(s, "r1q2_3") ||
        !Q_stricmp(s, "r1q2 3")) {
        return STEP_SMOOTH_R1Q2_3;
    }

    switch (cl_step_smoothing_mode->integer) {
    case 1:
        return STEP_SMOOTH_R1Q2_1;
    case 2:
        return STEP_SMOOTH_R1Q2_2;
    case 3:
        return STEP_SMOOTH_R1Q2_3;
    default:
        return STEP_SMOOTH_Q2PRO;
    }
}

/*
===================
CL_CheckPredictionError
===================
*/
void CL_CheckPredictionError(void)
{
    int         frame;
    int         delta[3];
    unsigned    cmd;
    int         len;

    if (cls.demo.playback) {
        return;
    }

    if (sv_paused->integer) {
        VectorClear(cl.prediction_error);
        return;
    }

    if (!cl_predict->integer || (cl.frame.ps.pmove.pm_flags & PMF_NO_PREDICTION))
        return;

    // calculate the last usercmd_t we sent that the server has processed
    frame = cls.netchan.incoming_acknowledged & CMD_MASK;
    cmd = cl.history[frame].cmdNumber;

    // compare what the server returned with what we had predicted it to be
    VectorSubtract(cl.frame.ps.pmove.origin, cl.predicted_origins[cmd & CMD_MASK], delta);

    // save the prediction error for interpolation
    len = abs(delta[0]) + abs(delta[1]) + abs(delta[2]);
    if (len <= 1 || len > 640) {
        // > 80 world units is a teleport or something
        VectorClear(cl.prediction_error);
        return;
    }

    SHOWMISS("prediction miss on %i: %i (%d %d %d)\n",
             cl.frame.number, len, delta[0], delta[1], delta[2]);

    // don't predict steps against server returned data
    if (cl.predicted_step_frame <= cmd)
        cl.predicted_step_frame = cmd + 1;

    VectorCopy(cl.frame.ps.pmove.origin, cl.predicted_origins[cmd & CMD_MASK]);

    // save for error interpolation
    VectorScale(delta, 0.125f, cl.prediction_error);
}

/*
====================
CL_ClipMoveToEntities
====================
*/
static void CL_ClipMoveToEntities(trace_t *tr, const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs, int contentmask)
{
    int         i;
    trace_t     trace;
    const mnode_t   *headnode;
    const centity_t *ent;
    const mmodel_t  *cmodel;

    for (i = 0; i < cl.numSolidEntities; i++) {
        ent = cl.solidEntities[i];

        if (cl.csr.extended && ent->current.number <= cl.maxclients && !(contentmask & CONTENTS_PLAYER))
            continue;

        if (ent->current.solid == PACKED_BSP) {
            // special value for bmodel
            cmodel = cl.model_clip[ent->current.modelindex];
            if (!cmodel)
                continue;
            headnode = cmodel->headnode;
        } else {
            headnode = CM_HeadnodeForBox(ent->mins, ent->maxs);
        }

        if (tr->allsolid)
            return;

        CM_TransformedBoxTrace(&trace, start, end,
                               mins, maxs, headnode, contentmask,
                               ent->current.origin, ent->current.angles,
                               cl.csr.extended);

        CM_ClipEntity(tr, &trace, (struct edict_s *)ent);
    }
}

/*
================
CL_Trace
================
*/
void CL_Trace(trace_t *tr, const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs, int contentmask)
{
    // check against world
    CM_BoxTrace(tr, start, end, mins, maxs, cl.bsp->nodes, contentmask, cl.csr.extended);
    tr->ent = (struct edict_s *)cl_entities;
    if (tr->fraction == 0)
        return;     // blocked by the world

    // check all other solid models
    CL_ClipMoveToEntities(tr, start, end, mins, maxs, contentmask);
}

static int pm_clipmask;

static trace_t q_gameabi CL_PMTrace(const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int contentmask)
{
    trace_t t;
    CL_Trace(&t, start, end, mins, maxs, (cl.csr.extended && contentmask) ? contentmask : pm_clipmask);
    return t;
}

static int CL_PointContents(const vec3_t point)
{
    const centity_t *ent;
    const mmodel_t  *cmodel;
    int i, contents;

    contents = CM_PointContents(point, cl.bsp->nodes, cl.csr.extended);

    for (i = 0; i < cl.numSolidEntities; i++) {
        ent = cl.solidEntities[i];

        if (ent->current.solid != PACKED_BSP) // special value for bmodel
            continue;

        cmodel = cl.model_clip[ent->current.modelindex];
        if (!cmodel)
            continue;

        contents |= CM_TransformedPointContents(point, cmodel->headnode, ent->current.origin,
                                                ent->current.angles, cl.csr.extended);
    }

    return contents;
}

/*
=================
CL_PredictMovement

Sets cl.predicted_origin and cl.predicted_angles
=================
*/
void CL_PredictAngles(void)
{
    cl.predicted_angles[0] = cl.viewangles[0] + SHORT2ANGLE(cl.frame.ps.pmove.delta_angles[0]);
    cl.predicted_angles[1] = cl.viewangles[1] + SHORT2ANGLE(cl.frame.ps.pmove.delta_angles[1]);
    cl.predicted_angles[2] = cl.viewangles[2] + SHORT2ANGLE(cl.frame.ps.pmove.delta_angles[2]);
}

static void CL_DetectQ2ProStep(const pmove_t *pm, unsigned frame)
{
    int step, oldz;

    if (pm->s.pm_type == PM_SPECTATOR || !(pm->s.pm_flags & PMF_ON_GROUND)) {
        return;
    }

    oldz = cl.predicted_origins[cl.predicted_step_frame & CMD_MASK][2];
    step = pm->s.origin[2] - oldz;
    if (step >= 63 && step < 160) {
        // check for stepping up before a previous step is completed
        unsigned delta = cls.realtime - cl.predicted_step_time;
        float prev_step = 0;
        if (delta < 100)
            prev_step = cl.predicted_step * (100 - delta) * 0.01f;

        cl.predicted_step = min(prev_step + step * 0.125f, 32);
        cl.predicted_step_time = cls.realtime;
        cl.predicted_step_frame = frame + 1;    // don't double step
    }
}

static void CL_DetectR1Q2Step(const pmove_t *pm, unsigned ack, unsigned current)
{
    stepSmoothMode_t mode = CL_StepSmoothMode();
    unsigned oldframe;
    int step, oldz;

    if (pm->s.pm_type == PM_SPECTATOR || !(pm->s.pm_flags & PMF_ON_GROUND)) {
        return;
    }

    if (mode == STEP_SMOOTH_R1Q2_3) {
        step = pm->s.origin[2] - (int)(cl.predicted_origin[2] * 8);

        if (((step > 62 && step < 66) ||
             (step > 94 && step < 98) ||
             (step > 126 && step < 130)) &&
            !VectorCompare(pm->s.velocity, vec3_origin)) {
            cl.predicted_step = step * 0.125f;
            cl.predicted_step_time = cls.realtime - (int)(cls.frametime * 500);
        }
        return;
    }

    if (mode == STEP_SMOOTH_R1Q2_2) {
        ack--;
    }

    oldframe = (ack - 2) & CMD_MASK;
    oldz = cl.predicted_origins[oldframe][2];
    step = pm->s.origin[2] - oldz;

    if (r1q2_step_frame != current && step > 63 && step < 160) {
        cl.predicted_step = step * 0.125f;
        cl.predicted_step_time = cls.realtime - (int)(cls.frametime * 500);
        r1q2_step_frame = current;
    }
}

static void CL_DetectStep(const pmove_t *pm, unsigned ack, unsigned current, unsigned frame)
{
    if (CL_StepSmoothMode() == STEP_SMOOTH_Q2PRO) {
        CL_DetectQ2ProStep(pm, frame);

        if (cl.predicted_step_frame < frame) {
            cl.predicted_step_frame = frame;
        }
    } else {
        CL_DetectR1Q2Step(pm, ack, current);
    }
}

void CL_PredictMovement(void)
{
    unsigned    ack, current, frame = 0;
    pmove_t     pm;
    bool        ran;

    if (cls.state != ca_active) {
        return;
    }

    if (cls.demo.playback) {
        return;
    }

    if (sv_paused->integer) {
        return;
    }

    if (!cl_predict->integer || (cl.frame.ps.pmove.pm_flags & PMF_NO_PREDICTION)) {
        // just set angles
        CL_PredictAngles();
        return;
    }

    ack = cl.history[cls.netchan.incoming_acknowledged & CMD_MASK].cmdNumber;
    current = cl.cmdNumber;

    // if we are too far out of date, just freeze
    if (current - ack > CMD_BACKUP - 1) {
        SHOWMISS("%i: exceeded CMD_BACKUP\n", cl.frame.number);
        return;
    }

    if (!cl.cmd.msec && current == ack) {
        SHOWMISS("%i: not moved\n", cl.frame.number);
        return;
    }

    pm_clipmask = MASK_PLAYERSOLID;

    // remaster player collision rules
    if (cl.csr.extended) {
        if (cl.frame.ps.pmove.pm_type == PM_DEAD || cl.frame.ps.pmove.pm_type == PM_GIB)
            pm_clipmask = MASK_DEADSOLID;

        if (!(cl.frame.ps.pmove.pm_flags & PMF_IGNORE_PLAYER_COLLISION))
            pm_clipmask |= CONTENTS_PLAYER;
    }

    // copy current state to pmove
    memset(&pm, 0, sizeof(pm));
    pm.trace = CL_PMTrace;
    pm.pointcontents = CL_PointContents;
    pm.s = cl.frame.ps.pmove;
    pm.snapinitial = qtrue;

    ran = false;

    // run frames
    while (++ack <= current) {
        pm.cmd = cl.cmds[ack & CMD_MASK];
        PmoveNew(&pm, &cl.pmp);
        pm.snapinitial = qfalse;
        ran = true;
        frame = ack;

        // save for debug checking
        VectorCopy(pm.s.origin, cl.predicted_origins[ack & CMD_MASK]);
    }

    // run pending cmd
    if (cl.cmd.msec && !cl_movement_feel_mode->integer) {
        pm.cmd = cl.cmd;
        pm.cmd.forwardmove = cl.localmove[0];
        pm.cmd.sidemove = cl.localmove[1];
        pm.cmd.upmove = cl.localmove[2];
        PmoveNew(&pm, &cl.pmp);
        ran = true;
        frame = current;

        // save for debug checking
        VectorCopy(pm.s.origin, cl.predicted_origins[(current + 1) & CMD_MASK]);
    } else if (!ran) {
        frame = current - 1;
    }

    if (!ran) {
        VectorScale(pm.s.origin, 0.125f, cl.predicted_origin);
        VectorScale(pm.s.velocity, 0.125f, cl.predicted_velocity);
        CL_PredictAngles();
        return;
    }

    CL_DetectStep(&pm, ack, current, frame);

    // copy results out for rendering
    VectorScale(pm.s.origin, 0.125f, cl.predicted_origin);
    VectorScale(pm.s.velocity, 0.125f, cl.predicted_velocity);
    VectorCopy(pm.viewangles, cl.predicted_angles);
}
