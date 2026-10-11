/*
 * This file is part of libplacebo.
 *
 * libplacebo is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * libplacebo is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with libplacebo. If not, see <http://www.gnu.org/licenses/>.
 */

#include <math.h>
#include "common.h"
#include "color_volume.h"
#include "pl_thread.h"

struct volume_math {
    double l2r[3][3], y[3];
    double black, peak;
};

static double pq(double nits)
{
    double x = pow(fmax(nits / 10000.0, 0.0), 2610.0 / 16384.0);
    return pow((3424.0/4096.0 + 2413.0/128.0*x) /
               (1.0 + 2392.0/128.0*x), 2523.0/32.0);
}

static double eotf(double v, double *derivative)
{
    const double m1=2610.0/16384.0, m2=2523.0/32.0;
    const double c1=3424.0/4096.0, c2=2413.0/128.0, c3=2392.0/128.0;
    v = fmin(fmax(v, 1e-12), 1.0);
    double a=pow(v, 1.0/m2), d=c2-c3*a, r=fmax((a-c1)/d, 0.0);
    double l=10000.0*pow(r, 1.0/m1);
    if (derivative)
        *derivative=10000.0/m1*pow(r, 1.0/m1-1.0) *
                    (c2-c3*c1)/(d*d)/m2*pow(v, 1.0/m2-1.0);
    return l;
}

// Solve intensity at fixed IPT hue/chroma and CIE Y. This runs during LUT
// generation only, never once per displayed pixel.
static bool solve(const struct volume_math *m, double Y, double C,
                  double co, double si, double rgb[3])
{
    double pt[3], lms[3], I=pq(Y), lo=0.0, hi=1.0;
    for (int j=0; j<3; j++)
        pt[j]=C*(pl_ipt_ipt2lms.m[j][1]*co + pl_ipt_ipt2lms.m[j][2]*si);
    bool converged=false;
    for (int it=0; it<32; it++) {
        double y=0, dy=0;
        for (int j=0; j<3; j++) {
            double d;
            lms[j]=eotf(I*pl_ipt_ipt2lms.m[j][0]+pt[j], &d);
            y+=m->y[j]*lms[j];
            dy+=m->y[j]*d*pl_ipt_ipt2lms.m[j][0];
        }
        if (fabs(y-Y) <= fmax(1e-9, Y*1e-11)) {
            converged=true;
            break;
        }
        if (y<Y) lo=I; else hi=I;
        double next=I-(y-Y)/fmax(dy, 1e-12);
        I=next>=lo && next<=hi ? next : (lo+hi)*0.5;
    }
    for (int i=0; i<3; i++) {
        rgb[i]=0;
        for (int j=0; j<3; j++) rgb[i]+=m->l2r[i][j]*lms[j];
    }
    return converged;
}

static bool inside(const struct volume_math *m, const double rgb[3])
{
    for (int i=0; i<3; i++)
        if (!isfinite(rgb[i]) || rgb[i]<m->black || rgb[i]>m->peak)
            return false;
    return true;
}

static double boundary(const struct volume_math *m, double Y,
                       double co, double si)
{
    double lo=0, hi=1, rgb[3];
    // IPT constant-Y slices need not be convex. Bracket the first exit
    // from neutral before refining, rather than finding a later re-entry.
    for (int k=1; k<=256; k++) {
        hi=(double)k/256;
        if (!solve(m,Y,hi,co,si,rgb) || !inside(m,rgb)) break;
        lo=hi;
    }
    for (int k=0; k<26; k++) {
        double c=(lo+hi)*0.5;
        if (solve(m,Y,c,co,si,rgb) && inside(m,rgb)) lo=c;
        else hi=c;
    }
    return lo * 0.9995; // interpolation/float representation headroom
}

struct generate_volume {
    struct volume_math math;
    float *data;
    int begin, end;
    bool failed;
};

static PL_THREAD_VOID generate_volume(void *ptr)
{
    struct generate_volume *a=ptr;
    const struct volume_math *m=&a->math;
    const double range=m->peak-m->black;
    for (int h=a->begin; h<a->end; h++) {
        double angle=2*M_PI*h/(VP_VOLUME_H-1)-M_PI;
        double co=cos(angle), si=sin(angle);
        for (int y=0; y<VP_VOLUME_Y; y++) {
            double yn=(double)y/(VP_VOLUME_Y-1); yn*=yn;
            double Y=m->black+yn*range;
            double b=y==0 || y==VP_VOLUME_Y-1 ? 0 : boundary(m,Y,co,si);
            for (int c=0; c<VP_VOLUME_C; c++) {
                double rgb[3];
                if (!c || !b) {
                    rgb[0]=rgb[1]=rgb[2]=Y;
                } else if (!solve(m,Y,b*c/(VP_VOLUME_C-1),co,si,rgb)) {
                    a->failed=true;
                    rgb[0]=rgb[1]=rgb[2]=Y;
                }
                int idx=((h*VP_VOLUME_C+c)*VP_VOLUME_Y+y)*4;
                for (int j=0; j<3; j++) {
                    double v=(rgb[j]-m->black)/range;
                    // Only roundoff is allowed here. Failure aborts the shader;
                    // an invalid LUT must not be silently used as a color clip.
                    if (!isfinite(v) || v < -1e-6 || v > 1.0+1e-6)
                        a->failed=true;
                    a->data[idx+j]=(float)fmin(fmax(v,0.0),1.0);
                }
                a->data[idx+3]=(float)b;
            }
        }
    }
    PL_THREAD_RETURN();
}

bool vp_color_volume_generate(float *data, const struct vp_color_volume *params)
{
    if (!(params->black>=0 && params->peak>params->black)) return false;
    pl_matrix3x3 l2r=pl_ipt_lms2rgb(&params->prim);
    pl_matrix3x3 xyz=pl_get_rgb2xyz_matrix(&params->prim);
    struct volume_math m={.black=params->black, .peak=params->peak};
    double sum=xyz.m[1][0]+xyz.m[1][1]+xyz.m[1][2];
    for (int i=0; i<3; i++) for (int j=0; j<3; j++) {
        m.l2r[i][j]=l2r.m[i][j];
        m.y[j]+=xyz.m[1][i]/sum*l2r.m[i][j];
    }
    enum { N=16 };
    struct generate_volume args[N];
    pl_thread threads[N]={0};
    bool ok=true;
    for (int i=0; i<N; i++) {
        args[i]=(struct generate_volume){.math=m, .data=data,
            .begin=VP_VOLUME_H*i/N, .end=VP_VOLUME_H*(i+1)/N};
        if (pl_thread_create(&threads[i],generate_volume,&args[i]))
            generate_volume(&args[i]);
    }
    for (int i=0; i<N; i++) {
        if (threads[i] && pl_thread_join(threads[i])) ok=false;
        if (args[i].failed) ok=false;
    }
    return ok;
}
