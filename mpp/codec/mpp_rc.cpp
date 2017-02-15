/*
 * Copyright 2016 Rockchip Electronics Co. LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define MODULE_TAG "mpp_rc"

#include "mpp_env.h"
#include "mpp_mem.h"
#include "mpp_common.h"
#include "mpp_rc.h"

#define MPP_RC_DBG_FUNCTION          (0x00000001)
#define MPP_RC_DBG_RC                (0x00000010)
#define MPP_RC_DBG_CFG               (0x00000100)

#define mpp_rc_dbg(flag, fmt, ...)   _mpp_dbg(mpp_rc_debug, flag, fmt, ## __VA_ARGS__)
#define mpp_rc_dbg_f(flag, fmt, ...) _mpp_dbg_f(mpp_rc_debug, flag, fmt, ## __VA_ARGS__)

#define mpp_rc_dbg_func(fmt, ...)    mpp_rc_dbg_f(MPP_RC_DBG_FUNCTION, fmt, ## __VA_ARGS__)
#define mpp_rc_dbg_rc(fmt, ...)      mpp_rc_dbg(MPP_RC_DBG_RC, fmt, ## __VA_ARGS__)
#define mpp_rc_dbg_cfg(fmt, ...)     mpp_rc_dbg(MPP_RC_DBG_CFG, fmt, ## __VA_ARGS__)

#define SIGN(a)         ((a) < (0) ? (-1) : (1))
#define DIV(a, b)       (((a) + (SIGN(a) * (b)) / 2) / (b))

static RK_U32 mpp_rc_debug = 0;

MPP_RET mpp_data_init(MppData **data, RK_S32 size)
{
    if (NULL == data || size <= 0) {
        mpp_err_f("invalid data %data size %d\n", data, size);
        return MPP_ERR_NULL_PTR;
    }

    *data = NULL;
    MppData *p = mpp_malloc_size(MppData, sizeof(MppData) + sizeof(RK_S32) * size);
    if (NULL == p) {
        mpp_err_f("malloc size %d failed\n", size);
        return MPP_ERR_MALLOC;
    }
    p->size = size;
    p->len = 0;
    p->pos = 0;
    p->val = (RK_S32 *)(p + 1);
    *data = p;

    return MPP_OK;
}

void mpp_data_deinit(MppData *p)
{
    if (p)
        mpp_free(p);
}

void mpp_data_update(MppData *p, RK_S32 val)
{
    mpp_assert(p);

    p->val[p->pos] = val;

    if (++p->pos >= p->size)
        p->pos = 0;

    if (p->len < p->size)
        p->len++;
}

RK_S32 mpp_data_avg(MppData *p, RK_S32 len, RK_S32 num, RK_S32 denorm)
{
    mpp_assert(p);

    RK_S32 i;
    RK_S32 sum = 0;
    RK_S32 pos = p->pos;

    if (!p->len)
        return 0;

    if (len < 0 || len > p->len)
        len = p->len;

    if (num == denorm) {
        i = len;
        while (i--) {
            if (pos)
                pos--;
            else
                pos = p->len - 1;

            sum += p->val[pos];
        }
    } else {
        mpp_assert(num > denorm);
        RK_S32 acc_num = num;
        RK_S32 acc_denorm = denorm;

        i = len - 1;
        sum = p->val[--pos];
        while (i--) {
            if (pos)
                pos--;
            else
                pos = p->len - 1;

            sum += p->val[pos] * acc_num / acc_denorm;
            acc_num += num;
            acc_denorm += denorm;
        }
    }
    return DIV(sum, len);
}

void mpp_pid_reset(MppPIDCtx *p)
{
    p->p = 0;
    p->i = 0;
    p->d = 0;
    p->count = 0;
}

void mpp_pid_set_param(MppPIDCtx *ctx, RK_S32 coef_p, RK_S32 coef_i, RK_S32 coef_d, RK_S32 div, RK_S32 len)
{
    ctx->coef_p = coef_p;
    ctx->coef_i = coef_i;
    ctx->coef_d = coef_d;
    ctx->div = div;
    ctx->len = len;
    ctx->count = 0;

    mpp_rc_dbg_rc("RC: pid ctx %p coef: P %d I %d D %d div %d len %d\n",
                  ctx, coef_p, coef_i, coef_d, div, len);
}

void mpp_pid_update(MppPIDCtx *ctx, RK_S32 val)
{
    mpp_rc_dbg_rc("RC: pid ctx %p update val %d\n", ctx, val);
    mpp_rc_dbg_rc("RC: pid ctx %p before update P %d I %d D %d\n", ctx, ctx->p, ctx->i, ctx->d);

    ctx->d = val - ctx->p;      /* Derivative */
    ctx->i = val + ctx->i;      /* Integral */
    ctx->p = val;               /* Proportional */

    mpp_rc_dbg_rc("RC: pid ctx %p after  update P %d I %d D %d\n", ctx, ctx->p, ctx->i, ctx->d);
    ctx->count++;
    /*
     * pid control is a short time control, it needs periodically reset
     */
    if (ctx->count >= ctx->len)
        mpp_pid_reset(ctx);
}

RK_S32 mpp_pid_calc(MppPIDCtx *p)
{
    RK_S32 a = p->p * p->coef_p + p->i * p->coef_i + p->d * p->coef_d;
    RK_S32 b = p->div;

    mpp_rc_dbg_rc("RC: pid ctx %p p %10d coef %d\n", p, p->p, p->coef_p);
    mpp_rc_dbg_rc("RC: pid ctx %p i %10d coef %d\n", p, p->i, p->coef_i);
    mpp_rc_dbg_rc("RC: pid ctx %p d %10d coef %d\n", p, p->d, p->coef_d);
    mpp_rc_dbg_rc("RC: pid ctx %p a %10d b %d\n", p, a, b);

    return DIV(a, b);
}

MPP_RET mpp_rc_init(MppRateControl **ctx)
{
    if (NULL == ctx) {
        mpp_log_f("invalid ctx %p\n", ctx);
        return MPP_ERR_NULL_PTR;
    }

    MPP_RET ret = MPP_OK;
    MppRateControl *p = mpp_calloc(MppRateControl, 1);
    if (NULL == ctx) {
        mpp_log_f("malloc failed\n");
        ret = MPP_ERR_MALLOC;
    } else {
        p->gop = -1;
    }

    mpp_env_get_u32("mpp_rc_debug", &mpp_rc_debug, 0);

    *ctx = p;
    return ret;
}

MPP_RET mpp_rc_deinit(MppRateControl *ctx)
{
    if (NULL == ctx) {
        mpp_log_f("invalid ctx %p\n", ctx);
        return MPP_ERR_NULL_PTR;
    }

    if (ctx->intra) {
        mpp_data_deinit(ctx->intra);
        ctx->intra = NULL;
    }

    if (ctx->gop_bits) {
        mpp_data_deinit(ctx->gop_bits);
        ctx->gop_bits = NULL;
    }

    if (ctx->intra_percent) {
        mpp_data_deinit(ctx->intra_percent);
        ctx->intra_percent = NULL;
    }

    mpp_free(ctx);
    return MPP_OK;
}

MPP_RET mpp_rc_update_user_cfg(MppRateControl *ctx, MppEncRcCfg *cfg, RK_S32 force_idr)
{
    if (NULL == ctx || NULL == cfg) {
        mpp_log_f("invalid ctx %p cfg %p\n", ctx, cfg);
        return MPP_ERR_NULL_PTR;
    }

    RK_U32 change = cfg->change;
    RK_U32 clear_acc = 0;
    RK_U32 gop_start = 0;

    /*
     * step 1: update parameters
     */
    if (change & MPP_ENC_RC_CFG_CHANGE_BPS) {
        mpp_rc_dbg_cfg("bps: %d [%d %d]\n", cfg->bps_target,
                       cfg->bps_min, cfg->bps_max);
        ctx->bps_min = cfg->bps_min;
        ctx->bps_max = cfg->bps_max;
        ctx->bps_target = cfg->bps_target;
    }

    if (change & MPP_ENC_RC_CFG_CHANGE_FPS_OUT) {
        mpp_rc_dbg_cfg("fps: %d / %d\n", cfg->fps_out_num, cfg->fps_out_denorm);
        ctx->fps_num = cfg->fps_out_num;
        ctx->fps_denom = cfg->fps_out_denorm;
        if (ctx->fps_denom == 0) {
            mpp_err("denorm can not be 0, change to default 1");
            ctx->fps_denom = 1;
        }
        ctx->fps_out = ctx->fps_num / ctx->fps_denom;
        if (ctx->fps_out == 0) {
            mpp_err("fps out can not be 0, change to default 30");
            ctx->fps_out = 30;
            ctx->fps_num = 30;
            ctx->fps_denom = 1;
        }
        clear_acc = 1;
    }

    if ((change & MPP_ENC_RC_CFG_CHANGE_GOP) &&
        (ctx->gop != cfg->gop)) {
        RK_S32 gop = cfg->gop;

        mpp_rc_dbg_cfg("gop: %d\n", cfg->gop);

        if (ctx->intra)
            mpp_data_deinit(ctx->intra);
        mpp_data_init(&ctx->intra, gop);

        if (ctx->gop_bits)
            mpp_data_deinit(ctx->gop_bits);
        mpp_data_init(&ctx->gop_bits, gop);

        if (ctx->intra_percent)
            mpp_data_deinit(ctx->intra_percent);
        mpp_data_init(&ctx->intra_percent, gop);

        ctx->gop = gop;
        if (gop < ctx->fps_out)
            ctx->window_len = ctx->fps_out;
        else
            ctx->window_len = gop;

        if (ctx->window_len < 10)
            ctx->window_len = 10;

        if (ctx->window_len > ctx->fps_out)
            ctx->window_len = ctx->fps_out;

        mpp_pid_reset(&ctx->pid_intra);
        mpp_pid_reset(&ctx->pid_inter);
        mpp_pid_set_param(&ctx->pid_intra, 4, 6, 0, 100, ctx->window_len);
        mpp_pid_set_param(&ctx->pid_inter, 4, 6, 0, 100, ctx->window_len);
        mpp_pid_set_param(&ctx->pid_fps, 4, 6, 0, 100, ctx->window_len);

        clear_acc = 1;

        /* if gop cfg changed, current frame is regarded as IDR frame */
        gop_start = 1;
    }

    /*
     * step 2: derivate parameters
     */
    ctx->bits_per_pic = axb_div_c(cfg->bps_target, ctx->fps_denom, ctx->fps_num);
    mpp_rc_dbg_rc("RC: rc ctx %p target bit per picture %d\n", ctx, ctx->bits_per_pic);

    if (clear_acc) {
        ctx->acc_intra_bits_in_fps = 0;
        ctx->acc_inter_bits_in_fps = 0;
        ctx->acc_total_bits = 0;
        ctx->acc_intra_count = 0;
        ctx->acc_inter_count = 0;
        ctx->last_fps_bits = 0;
    }

    RK_S32 gop = ctx->gop;
    RK_S32 avg = ctx->bits_per_pic;

    if (clear_acc) {
        if (gop == 0) {
            /* only one intra then all inter */
            ctx->gop_mode = MPP_GOP_ALL_INTER;
            ctx->bits_per_inter = avg;
            ctx->bits_per_intra = avg * 10;
            ctx->intra_to_inter_rate = 0;
        } else if (gop == 1) {
            /* all intra */
            ctx->gop_mode = MPP_GOP_ALL_INTRA;
            ctx->bits_per_inter = 0;
            ctx->bits_per_intra = avg;
            ctx->intra_to_inter_rate = 0;
            gop_start = 1;
        } else if (ctx->gop < ctx->window_len) {
            /* small gop - use fix allocation */
            ctx->gop_mode = MPP_GOP_SMALL;
            ctx->intra_to_inter_rate = gop + 1;
            ctx->bits_per_inter = avg / 2;
            ctx->bits_per_intra = ctx->bits_per_inter * ctx->intra_to_inter_rate;
        } else {
            /* large gop - use dynamic allocation */
            RK_U32 intra_to_inter_rate;
            ctx->gop_mode = MPP_GOP_LARGE;
            mpp_env_get_u32("intra_rate", &intra_to_inter_rate, 3);
            ctx->intra_to_inter_rate = intra_to_inter_rate;
            ctx->bits_per_inter = ctx->bits_per_pic;
            ctx->bits_per_intra = ctx->bps_target * 2 / 3;
            ctx->bits_per_inter -= ctx->bits_per_intra / (ctx->fps_out - 1);
        }
    }

    if (ctx->acc_total_count == gop)
        gop_start = 1;

    if (force_idr) {
        ctx->cur_frmtype = INTRA_FRAME;
    } else {
        if (gop_start)
            ctx->cur_frmtype = INTRA_FRAME;
        else
            ctx->cur_frmtype = INTER_P_FRAME;
    }
    if (ctx->cur_frmtype == INTRA_FRAME) {
        ctx->acc_total_count = 0;
        ctx->acc_inter_bits_in_fps = 0;
        ctx->acc_intra_bits_in_fps = 0;
    }

    cfg->change = 0;

    return MPP_OK;
}

MPP_RET mpp_rc_bits_allocation(MppRateControl *ctx, RcSyntax *rc_syn)
{
    if (NULL == ctx || NULL == rc_syn) {
        mpp_log_f("invalid ctx %p rc_syn %p\n", ctx, rc_syn);
        return MPP_ERR_NULL_PTR;
    }

    /* step 1: calc target frame bits */
    switch (ctx->gop_mode) {
    case MPP_GOP_ALL_INTER : {
        if (ctx->cur_frmtype == INTRA_FRAME)
            ctx->bits_target = ctx->bits_per_intra;
        else
            ctx->bits_target = ctx->bits_per_inter - mpp_pid_calc(&ctx->pid_inter);
    } break;
    case MPP_GOP_ALL_INTRA : {
        ctx->bits_target = ctx->bits_per_intra - mpp_pid_calc(&ctx->pid_intra);
    } break;
    default : {
        if (ctx->cur_frmtype == INTRA_FRAME) {
            float intra_percent = 0.0;
            RK_S32 diff_bit = mpp_pid_calc(&ctx->pid_fps);
            /* only affected by last gop */
            mpp_pid_reset(&ctx->pid_fps);

            if (ctx->acc_intra_count) {
                intra_percent = mpp_data_avg(ctx->intra_percent, 1, 1, 1) / 100.0;
                ctx->last_intra_percent = intra_percent;
            }

            if (ctx->acc_intra_count) {
                ctx->bits_target = (ctx->fps_out * ctx->bits_per_pic + diff_bit) * intra_percent;
            } else
                ctx->bits_target = ctx->bits_per_intra - mpp_pid_calc(&ctx->pid_intra);
        } else {
            if (ctx->pre_frmtype == INTRA_FRAME) {
                RK_S32 diff_bit = mpp_pid_calc(&ctx->pid_fps);
                /*
                 * case - inter frame after intra frame
                 * update inter target bits with compensation of previous intra frame
                 */
                RK_S32 bits_prev_intra = mpp_data_avg(ctx->intra, 1, 1, 1);

                ctx->bits_per_inter = (ctx->bps_target *
                                       (ctx->gop * 1.0 / ctx->fps_out) -
                                       bits_prev_intra + diff_bit *
                                       (1 - ctx->last_intra_percent)) /
                                      (ctx->gop - 1);

                mpp_rc_dbg_rc("RC: rc ctx %p bits pic %d win %d intra %d inter %d\n",
                              ctx, ctx->bits_per_pic, ctx->window_len,
                              bits_prev_intra, ctx->bits_per_inter);

                mpp_rc_dbg_rc("RC: rc ctx %p update inter target bits to %d\n",
                              ctx, ctx->bits_per_inter);
                ctx->bits_target = ctx->bits_per_inter;
            } else {
                RK_S32 diff_bit = mpp_pid_calc(&ctx->pid_inter);
                ctx->bits_target = ctx->bits_per_inter - diff_bit;

                mpp_rc_dbg_rc("RC: rc ctx %p inter pid diff %d target %d\n",
                              ctx, diff_bit, ctx->bits_target);
            }
        }
    } break;
    }

    ctx->bits_target = ctx->bits_target > 0 ? ctx->bits_target : 0;

    rc_syn->bit_target = ctx->bits_target;

    rc_syn->type = ctx->cur_frmtype;
    if (ctx->cur_frmtype == INTRA_FRAME) {
        mpp_rc_dbg_rc("RC: rc ctx %p intra target bits %d\n", ctx, ctx->bits_target);
    } else {
        mpp_rc_dbg_rc("RC: rc ctx %p inter target bits %d\n", ctx, ctx->bits_target);
    }

    /* step 2: calc min and max bits */

    return MPP_OK;
}

MPP_RET mpp_rc_update_hw_result(MppRateControl *ctx, RcHalResult *result)
{
    if (NULL == ctx || NULL == result) {
        mpp_log_f("invalid ctx %p result %p\n", ctx, result);
        return MPP_ERR_NULL_PTR;
    }

    RK_S32 bits = result->bits;

    if (result->type == INTRA_FRAME) {
        mpp_rc_dbg_rc("RC: rc ctx %p intra real bits %d target %d\n",
                      ctx, bits, ctx->bits_per_intra);
        ctx->acc_intra_count++;
        ctx->acc_intra_bits_in_fps += bits;
        mpp_data_update(ctx->intra, bits);
        mpp_data_update(ctx->gop_bits, bits);
        mpp_pid_update(&ctx->pid_intra, bits - ctx->bits_target);
    } else {
        mpp_rc_dbg_rc("RC: rc ctx %p inter real bits %d target %d\n",
                      ctx, bits, ctx->bits_per_inter);
        ctx->acc_inter_count++;
        ctx->acc_inter_bits_in_fps += bits;
        mpp_data_update(ctx->gop_bits, bits);
        mpp_pid_update(&ctx->pid_inter, bits - ctx->bits_target);
    }
    ctx->acc_total_count++;
    ctx->last_fps_bits += bits;

    /* new fps start */
    if ((ctx->acc_intra_count + ctx->acc_inter_count) % ctx->fps_out == 0) {
        mpp_pid_update(&ctx->pid_fps, ctx->bps_target - ctx->last_fps_bits);
        if (ctx->acc_intra_bits_in_fps && ctx->acc_inter_bits_in_fps)
            mpp_data_update(ctx->intra_percent,
                            ctx->acc_intra_bits_in_fps * 100 /
                            (ctx->acc_inter_bits_in_fps + ctx->acc_intra_bits_in_fps));
        ctx->acc_intra_bits_in_fps = 0;
        ctx->acc_inter_bits_in_fps = 0;
        ctx->last_fps_bits = 0;
    }

    switch (ctx->gop_mode) {
    case MPP_GOP_ALL_INTER : {
    } break;
    case MPP_GOP_ALL_INTRA : {
    } break;
    default : {
    } break;
    }

    ctx->pre_frmtype = ctx->cur_frmtype;

    return MPP_OK;
}

MPP_RET mpp_linreg_init(MppLinReg **ctx, RK_S32 size, RK_S32 weight_mode)
{
    if (NULL == ctx) {
        mpp_log_f("invalid ctx %p\n", ctx);
        return MPP_ERR_NULL_PTR;
    }

    MPP_RET ret = MPP_OK;
    MppLinReg *p = mpp_calloc_size(MppLinReg,
                                   sizeof(MppLinReg) +
                                   size * (sizeof(RK_S32) * 2 + sizeof(RK_S64)));
    if (NULL == p) {
        mpp_log_f("malloc failed\n");
        ret = MPP_ERR_MALLOC;
    } else {
        p->x = (RK_S32 *)(p + 1);
        p->r = p->x + size;
        p->y = (RK_S64 *)(p->r + size);
        p->size = size;
        p->weight_mode = weight_mode;
    }

    *ctx = p;
    return ret;
}

MPP_RET mpp_linreg_deinit(MppLinReg *ctx)
{
    if (NULL == ctx) {
        mpp_log_f("invalid ctx %p\n", ctx);
        return MPP_ERR_NULL_PTR;
    }

    mpp_free(ctx);
    return MPP_OK;
}

static RK_S64 linreg_weight[6][15] = {
    {1000, 500, 250, 125, 63, 31, 16, 8, 4, 2, 1, 0, 0, 0, 0},
    {1000, 600, 360, 216, 130, 78, 47, 28, 17, 10, 6, 4, 2, 1, 0},
    {1000, 700, 490, 343, 240, 168, 117, 82, 57, 40, 32, 26, 20, 16, 13},
    {1000, 800, 640, 512, 409, 328, 262, 210, 168, 134, 107, 86, 69, 55, 44},
    {1000, 900, 810, 729, 656, 590, 531, 478, 430, 387, 348, 313, 282, 254, 229},
    {1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000}
};

MPP_RET mpp_quadreg_update(MppLinReg *ctx, RK_S32 x, RK_S32 r, RK_S32 wlen)
{
    double a00 = 0.0, a01 = 0.0, a02 = 0.0, a10 = 0.0, a11 = 0.0, a12 = 0.0, a20 = 0.0, a21 = 0.0, a22 = 0.0;
    double ta00, ta01, ta02, ta10, ta11, ta12, ta20, ta21, ta22;
    double b0 = 0.0, b1 = 0.0, b2 = 0.0;
    double matrix_value = 0.0;

    RK_S32 cq = 0;
    RK_S32 est_b = 0;
    RK_S32 w = 0;

    double a = 0.0, b = 0.0, c = 0.0;

    /* step 1: save data */
    RK_S64 y = (RK_S64)x * x * r;
    ctx->x[ctx->i] = x;
    ctx->r[ctx->i] = r;
    ctx->y[ctx->i] = y;

    mpp_rc_dbg_rc("RC: linreg %p save index %d x %d r %d x*x*r %lld\n",
                  ctx, ctx->i, x, r, y);

    if (++ctx->i >= ctx->size)
        ctx->i = 0;

    if (ctx->n < ctx->size)
        ctx->n++;

    /* step 2: update coefficient */
    RK_S32 i = 0;
    RK_S32 n;

    RK_S32 *cx = ctx->x;
    RK_S32 *cr = ctx->r;
    RK_S32 idx = 0;

    n = ctx->n;
    i = ctx->i;

    while (n--) {
        if (i == 0)
            i = ctx->size - 1;
        else
            i--;

        cq = cx[i];

        /* limite wlen when complexity change sharply */
        if (idx++ > wlen)
            break;
    }

    w = idx;
    idx = 0;
    n = w;
    i = ctx->i;

    while (n--) {
        if (i == 0)
            i = ctx->size - 1;
        else
            i--;

        if (cq != cx[i])
            est_b = 1;

        a += 1.0 * cx[i] * cr[i] / w;
    }

    if (est_b && w >= 1) {
        n = w;
        i = ctx->i;
        while (n--) {
            if (i == 0)
                i = ctx->size - 1;
            else
                i--;

            double wt = linreg_weight[ctx->weight_mode][idx++] * 1000.0;

            a00 += wt;
            a01 += wt / cx[i];
            a02 += wt / cx[i] / cx[i];
            a10  = a01;
            a11  = a02;
            a12 += wt / cx[i] / cx[i] / cx[i];
            a20  = a02;
            a21  = a12;
            a22 += wt / cx[i] / cx[i] / cx[i] / cx[i];

            b0 += wt * cr[i];
            b1 += wt * cr[i] / cx[i];
            b2 += wt * cr[i] / cx[i] / cx[i];
        }

        ta00 = a11 * a22 - a21 * a12;
        ta01 = -(a01 * a22 - a21 * a02);
        ta02 = a01 * a12 - a11 * a02;
        ta10 = -(a10 * a22 - a20 * a12);
        ta11 = a00 * a22 - a20 * a02;
        ta12 = -(a00 * a02 - a10 * a02);
        ta20 = a10 * a21 - a20 * a11;
        ta21 = -(a00 * a21 - a20 * a01);
        ta22 = a00 * a11 - a10 * a01;

        mpp_rc_dbg_rc("matrix A:\n");

        mpp_rc_dbg_rc("%e %e %e\n", a00, a01, a02);
        mpp_rc_dbg_rc("%e %e %e\n", a10, a11, a12);
        mpp_rc_dbg_rc("%e %e %e\n", a20, a21, a22);

        mpp_rc_dbg_rc("adjoint matrix of A:\n");

        mpp_rc_dbg_rc("%e %e %e\n", ta00, ta01, ta02);
        mpp_rc_dbg_rc("%e %e %e\n", ta10, ta11, ta12);
        mpp_rc_dbg_rc("%e %e %e\n", ta20, ta21, ta22);

        mpp_rc_dbg_rc("vector B:\n");

        mpp_rc_dbg_rc("%e %e %e\n", b0, b1, b2);

        matrix_value = a00 * a11 * a22 + a01 * a12 * a20 + a02 * a10 * a21
                       - a02 * a11 * a20 - a00 * a12 * a21 - a01 * a10 * a22;

        mpp_rc_dbg_rc("matrix value: %e\n", matrix_value);

        if (matrix_value < 0.000001)
            matrix_value = 0.000001;

        a = (ta00 * b0 + ta01 * b1 + ta02 * b2) / matrix_value;
        b = (ta10 * b0 + ta11 * b1 + ta12 * b2) / matrix_value;
        c = (ta20 * b0 + ta21 * b1 + ta22 * b2) / matrix_value;
    }

    ctx->a = a;
    ctx->b = b;
    ctx->c = c;

    mpp_rc_dbg_rc("quadreg: a %e b %e c %e\n", a, b, c);

    return MPP_OK;
}

/*
 * This function want to calculate coefficient 'b' 'a' using ordinary
 * least square.
 * y = b * x + a
 * b_n = accumulate(x * y) - n * (average(x) * average(y))
 * a_n = accumulate(x * x) * accumulate(y) - accumulate(x) * accumulate(x * y)
 * denom = accumulate(x * x) - n * (square(average(x))
 * b = b_n / denom
 * a = a_n / denom
 */
MPP_RET mpp_linreg_update(MppLinReg *ctx, RK_S32 x, RK_S32 r)
{
    if (NULL == ctx) {
        mpp_log_f("invalid ctx %p\n", ctx);
        return MPP_ERR_NULL_PTR;
    }

    /* step 1: save data */
    RK_S64 y = (RK_S64)x * x * r;
    ctx->x[ctx->i] = x;
    ctx->r[ctx->i] = r;
    ctx->y[ctx->i] = y;

    mpp_rc_dbg_rc("RC: linreg %p save index %d x %d r %d x*x*r %lld\n",
                  ctx, ctx->i, x, r, y);

    if (++ctx->i >= ctx->size)
        ctx->i = 0;

    if (ctx->n < ctx->size)
        ctx->n++;

    /* step 2: update coefficient */
    RK_S32 i = 0;
    RK_S32 n;
    RK_S64 acc_xy = 0;
    RK_S64 acc_x = 0;
    RK_S64 acc_y = 0;
    RK_S64 acc_sq_x = 0;

    RK_S64 b_num = 0;
    RK_S64 denom = 0;

    RK_S32 *cx = ctx->x;
    RK_S64 *cy = ctx->y;
    RK_S64 ws = 0;
    RK_S32 idx = 0;
    RK_S64 w;

    n = ctx->n;
    i = ctx->i;

    while (n--) {
        if (i == 0)
            i = ctx->size - 1;
        else
            i--;

        w = linreg_weight[ctx->weight_mode][idx++];
        ws += w;
        acc_xy += w * cx[i] * cy[i];
        acc_x += w * cx[i];
        acc_y += w * cy[i];
        acc_sq_x += w * cx[i] * cx[i];
    }

    b_num = acc_xy - acc_y * acc_x / ws;
    denom = acc_sq_x - acc_x * acc_x / ws;

    mpp_rc_dbg_rc("RC: linreg %p acc_xy %lld acc_x %lld acc_y %lld acc_sq_x %lld\n",
                  ctx, acc_xy, acc_x, acc_y, acc_sq_x);
    mpp_rc_dbg_rc("RC: linreg %p n %d b_num %lld denom %lld\n",
                  ctx, ctx->n, b_num, denom);

    mpp_rc_dbg_rc("RC: linreg %p before update coefficient a %d b %d\n",
                  ctx, ctx->a, ctx->b);

    if (denom)
        ctx->b = DIV(b_num, denom);
    else
        ctx->b = 0;

    ctx->a = DIV(acc_y - acc_x * ctx->b, ws);

    mpp_rc_dbg_rc("RC: linreg %p after  update coefficient a %d b %d\n",
                  ctx, ctx->a, ctx->b);

    return MPP_OK;
}

RK_S32 mpp_quadreg_calc(MppLinReg *ctx, RK_S32 x)
{
    if (x <= 0)
        return -1;

    return ctx->a + DIV(ctx->b, x) + DIV(ctx->c, x * x);
}

RK_S32 mpp_linreg_calc(MppLinReg *ctx, RK_S32 x)
{
    if (x <= 0)
        return -1;

    return DIV(ctx->b, x) + DIV(ctx->a, x * x);
}
