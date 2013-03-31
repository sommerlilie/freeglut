/*
 * freeglut_teapot.c
 *
 * Teapot(tm) rendering code.
 *
 * Copyright (c) 1999-2000 Pawel W. Olszta. All Rights Reserved.
 * Written by Pawel W. Olszta, <olszta@sourceforge.net>
 * Creation date: Fri Dec 24 1999
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PAWEL W. OLSZTA BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/* notes:
 * the (very little) required math is found here: http://www.gamasutra.com/view/feature/131848/tessellation_of_4x4_bezier_patches_.php?print=1
 * a much more optimized version is here, didn't bother to implement that: http://www.gamasutra.com/view/feature/131794/an_indepth_look_at_bicubic_bezier_.php?print=1
 * teacup and teaspoon data: ftp://ftp.funet.fi/pub/sci/graphics/packages/objects/teaset.tar.Z (figure out this sampling scheme)
 */

#include <GL/freeglut.h>
#include "fg_internal.h"
#include "fg_teapot_data.h"

/* -- STATIC VARS: CACHES ---------------------------------------------------- */

/* Teapot defs */
#define GLUT_TEAPOT_N_PATCHES       (6*4 + 4*2)                                                                                 /* 6 patches are reproduced (rotated) 4 times, 4 patches (flipped) 2 times */
#define GLUT_SOLID_TEAPOT_N_SUBDIV  10
#define GLUT_SOLID_TEAPOT_N_VERT    GLUT_SOLID_TEAPOT_N_SUBDIV*GLUT_SOLID_TEAPOT_N_SUBDIV * GLUT_TEAPOT_N_PATCHES               /* N_SUBDIV^2 vertices per patch */
#define GLUT_SOLID_TEAPOT_N_TRI     (GLUT_SOLID_TEAPOT_N_SUBDIV-1)*(GLUT_SOLID_TEAPOT_N_SUBDIV-1) * GLUT_TEAPOT_N_PATCHES * 2   /* if e.g. 7x7 vertices for each patch, there are 6*6 squares for each patch. Each square is decomposed into 2 triangles */

#define GLUT_WIRE_TEAPOT_N_SUBDIV   7
#define GLUT_WIRE_TEAPOT_N_VERT     GLUT_WIRE_TEAPOT_N_SUBDIV*GLUT_WIRE_TEAPOT_N_SUBDIV * GLUT_TEAPOT_N_PATCHES                 /* N_SUBDIV^2 vertices per patch */

/* Bernstein coefficients only have to be precomputed once (number of patch subdivisions is fixed)
 * Can thus define arrays for them here, they will be filled upon first use.
 * 3rd order Bezier surfaces have 4 Bernstein coeffs.
 * Have separate caches for solid and wire as they use a different number of subdivisions
 * _0 is for Bernstein polynomials, _1 for their first derivative (which we need for normals)
 */
static GLfloat bernWire_0 [GLUT_WIRE_TEAPOT_N_SUBDIV] [4];
static GLfloat bernWire_1 [GLUT_WIRE_TEAPOT_N_SUBDIV] [4];
static GLfloat bernSolid_0[GLUT_SOLID_TEAPOT_N_SUBDIV][4];
static GLfloat bernSolid_1[GLUT_SOLID_TEAPOT_N_SUBDIV][4];

/* Bit of caching:
 * vertex indices and normals only need to be generated once for
 * a given number of subdivisions as they don't change with scale.
 * Vertices can be cached and reused if scale didn't change.
 */
static GLushort vertIdxsSolid[GLUT_SOLID_TEAPOT_N_TRI*3];
static GLfloat  normsSolid   [GLUT_SOLID_TEAPOT_N_VERT*3];
static GLfloat  vertsSolid   [GLUT_SOLID_TEAPOT_N_VERT*3];
static GLfloat  texcsSolid   [GLUT_SOLID_TEAPOT_N_VERT*2];
static GLfloat  lastScaleSolid  = 0.f;
static GLboolean initedSolid    = GL_FALSE;

static GLushort vertIdxsWire [GLUT_WIRE_TEAPOT_N_VERT*2];
static GLfloat  normsWire    [GLUT_WIRE_TEAPOT_N_VERT*3];
static GLfloat  vertsWire    [GLUT_WIRE_TEAPOT_N_VERT*3];
static GLfloat  lastScaleWire   = 0.f;
static GLboolean initedWire     = GL_FALSE;

/* -- PRIVATE FUNCTIONS ---------------------------------------------------- */
extern void fghDrawGeometrySolid(GLfloat *vertices, GLfloat *normals, GLfloat *textcs, GLsizei numVertices,
                                 GLushort *vertIdxs, GLsizei numParts, GLsizei numVertIdxsPerPart);
extern void fghDrawGeometryWire(GLfloat *vertices, GLfloat *normals, GLsizei numVertices,
                                GLushort *vertIdxs, GLsizei numParts, GLsizei numVertPerPart, GLenum vertexMode,
                                GLushort *vertIdxs2, GLsizei numParts2, GLsizei numVertPerPart2);

/* evaluate 3rd order Bernstein polynomial and its 1st deriv */
static void bernstein3(int i, GLfloat x, GLfloat *r0, GLfloat *r1)
{
    float invx = 1.f - x;

    /* r0: zero order coeff, r1: first deriv coeff */
    switch (i)
    {
        GLfloat temp;
    case 0:
        temp = invx*invx;
        *r0 = invx * temp;                  /* invx * invx * invx */
        *r1 = -3 * temp;                    /*   -3 * invx * invx */
        break;
    case 1:
        temp = invx*invx;
        *r0 = 3 * x * temp;                 /* 3 * x * invx * invx */
        *r1 = 3 * temp  -  6 * x * invx;    /* 3 * invx * invx  -  6 * x * invx */
        break;
    case 2:
        temp = x*x;
        *r0 = 3 * temp * invx;              /* 3 * x * x * invx */
        *r1 = 6 * x * invx  -  3 * temp;    /* 6 * x * invx  -  3 * x * x */
        break;
    case 3:
        temp = x*x;
        *r0 = x * temp;                     /* x * x * x */
        *r1 = 3 * temp;                     /* 3 * x * x */
        break;
    default:
        *r0 = *r1 = 0;
    }
}

static void pregenBernstein(int nSubDivs, GLfloat (*bern_0)[4], GLfloat (*bern_1)[4])
{
    int s,i;
    for (s=0; s<nSubDivs; s++)
    {
        GLfloat x = s/(nSubDivs-1.f);
        for (i=0; i<4; i++) /* 3rd order polynomial */
            bernstein3(i,x,bern_0[s]+i,bern_1[s]+i);
    }
}

/* based on flag either rotate patches around y axis to other 3 quadrants (flag=4) or reflect patch across x-y plane (flag=2) */
static void rotOrReflect(int flag, int nVals, int nSubDivs, GLfloat *vals)
{
    int u,i,o;

    if (flag==4)
    {
        int i1=nVals, i2=nVals*2, i3=nVals*3;
        for (o=0; o<nVals; o+=3)
        {
            /* 90� rotation */
            vals[i1+o+0] =  vals[o+2];
            vals[i1+o+1] =  vals[o+1];
            vals[i1+o+2] = -vals[o+0];
            /* 180� rotation */
            vals[i2+o+0] = -vals[o+0];
            vals[i2+o+1] =  vals[o+1];
            vals[i2+o+2] = -vals[o+2];
            /* 270� rotation */
            vals[i3+o+0] = -vals[o+2];
            vals[i3+o+1] =  vals[o+1];
            vals[i3+o+2] =  vals[o+0];
        }
    }
    else if (flag==2)
    {
        /* copy over values, reversing row order to keep winding correct, and negating z to perform the flip */
        for (u=0; u<nSubDivs; u++)  /* per row */
        {
            int off =   (nSubDivs-u-1)*nSubDivs*3;  /* read last row first from the already existing rows */
            o       = nVals + u   *nSubDivs*3;      /* write last row as first row to output */
            for (i=0; i<nSubDivs*3; i+=3, o+=3)     /* each row has nSubDivs points consisting of three values */
            {
                vals[o+0] =  vals[off+i+0];
                vals[o+1] =  vals[off+i+1];
                vals[o+2] = -vals[off+i+2];
            }
        }
    }
}

/* verts array should be initialized to 0! */
static int evalBezierWithNorm(GLfloat cp[4][4][3], int nSubDivs, float (*bern_0)[4], float (*bern_1)[4], int flag, int normalFix, GLfloat *verts, GLfloat *norms)
{
    int nVerts    = nSubDivs*nSubDivs;
    int nVertVals = nVerts*3;               /* number of values output for one patch, flag (2 or 4) indicates how many times we will write this to output */
    int u,v,i,j,o;

    /* generate vertices and coordinates for the patch */
    for (u=0,o=0; u<nSubDivs; u++)
    {
        for (v=0; v<nSubDivs; v++, o+=3)
        {
            /* for normals, get two tangents at the vertex using partial derivatives of 2D Bezier grid */
            float tan1[3]={0}, tan2[3]={0}, len;
            for (i=0; i<=3; i++)
            {
                float vert_0[3]={0}, vert_1[3]={0};
                for (j=0; j<=3; j++)
                {
                    vert_0[0] += bern_0[v][j] * cp[i][j][0];
                    vert_0[1] += bern_0[v][j] * cp[i][j][1];
                    vert_0[2] += bern_0[v][j] * cp[i][j][2];

                    vert_1[0] += bern_1[v][j] * cp[i][j][0];
                    vert_1[1] += bern_1[v][j] * cp[i][j][1];
                    vert_1[2] += bern_1[v][j] * cp[i][j][2];
                }

                verts[o+0] += bern_0[u][i]*vert_0[0];
                verts[o+1] += bern_0[u][i]*vert_0[1];
                verts[o+2] += bern_0[u][i]*vert_0[2];

                tan1[0] += bern_0[u][i]*vert_1[0];
                tan1[1] += bern_0[u][i]*vert_1[1];
                tan1[2] += bern_0[u][i]*vert_1[2];
                tan2[0] += bern_1[u][i]*vert_0[0];
                tan2[1] += bern_1[u][i]*vert_0[1];
                tan2[2] += bern_1[u][i]*vert_0[2];
            }
            /* get normal through cross product of the two tangents of the vertex */
            norms[o+0] = tan1[1] * tan2[2] - tan1[2] * tan2[1];
            norms[o+1] = tan1[2] * tan2[0] - tan1[0] * tan2[2];
            norms[o+2] = tan1[0] * tan2[1] - tan1[1] * tan2[0];
            len = (GLfloat)sqrt(norms[o+0] * norms[o+0] + norms[o+1] * norms[o+1] + norms[o+2] * norms[o+2]);
            norms[o+0] /= len;
            norms[o+1] /= len;
            norms[o+2] /= len;
        }
    }

    /* Fix normal vector if needed */
    if (normalFix)
    {
        for (o=0; o<nSubDivs*3; o+=3) /* whole first row (first nSubDivs normals) is broken: replace normals for the whole row */
        {
            norms[o+0] = 0.f;
            norms[o+1] = normalFix==1? 1.f:-1.f;
            norms[o+2] = 0.f;
        }
    }

    /* now based on flag either rotate patches around y axis to other 3 quadrants (flag=4) or reflect patch across x-y plane (flag=2) */
    rotOrReflect(flag, nVertVals, nSubDivs, verts);
    rotOrReflect(flag, nVertVals, nSubDivs, norms);

    return nVertVals*flag;
}

/* verts array should be initialized to 0! */
static int evalBezier(GLfloat cp[4][4][3], int nSubDivs, float (*bern_0)[4], int flag, GLfloat *verts)
{
    int nVerts    = nSubDivs*nSubDivs;
    int nVertVals = nVerts*3;               /* number of values output for one patch, flag (2 or 4) indicates how many times we will write this to output */
    int u,v,i,j,o;

    /* generate vertices and coordinates for the patch */
    for (u=0,o=0; u<nSubDivs; u++)
    {
        for (v=0; v<nSubDivs; v++, o+=3)
        {
            for (i=0; i<=3; i++)
            {
                float vert_0[3]={0};
                for (j=0; j<=3; j++)
                {
                    vert_0[0] += bern_0[v][j] * cp[i][j][0];
                    vert_0[1] += bern_0[v][j] * cp[i][j][1];
                    vert_0[2] += bern_0[v][j] * cp[i][j][2];
                }

                verts[o+0] += bern_0[u][i]*vert_0[0];
                verts[o+1] += bern_0[u][i]*vert_0[1];
                verts[o+2] += bern_0[u][i]*vert_0[2];
            }
        }
    }

    /* now based on flag either rotate patches around y axis to other 3 quadrants (flag=4) or reflect patch across x-y plane (flag=2) */
    rotOrReflect(flag, nVertVals, nSubDivs, verts);

    return nVertVals*flag;
}

static void fghTeapot( GLfloat scale, GLboolean useWireMode )
{
    /* for internal use */
    int p,o;
    GLfloat cp[4][4][3];
    /* to hold pointers to static vars/arrays */
    GLfloat (*bern_0)[4], (*bern_1)[4];
    GLfloat *verts, *norms, *lastScale;
    GLushort *vertIdxs;
    GLboolean * inited;
    int nSubDivs, nVerts;

    /* Get relevant static arrays and variables */
    bern_0      = useWireMode ? bernWire_0                : bernSolid_0;
    bern_1      = useWireMode ? bernWire_1                : bernSolid_1;
    verts       = useWireMode ? vertsWire                 : vertsSolid;
    norms       = useWireMode ? normsWire                 : normsSolid;
    lastScale   = useWireMode ? &lastScaleWire            : &lastScaleSolid;
    vertIdxs    = useWireMode ? vertIdxsWire              : vertIdxsSolid;
    inited      = useWireMode ? &initedWire               : &initedSolid;
    nSubDivs    = useWireMode ? GLUT_WIRE_TEAPOT_N_SUBDIV : GLUT_SOLID_TEAPOT_N_SUBDIV;
    nVerts      = useWireMode ? GLUT_WIRE_TEAPOT_N_VERT   : GLUT_SOLID_TEAPOT_N_VERT;

    /* check if need to generate vertices */
    if (!*inited || scale != *lastScale)
    {
        printf("regen\n");
        /* set vertex array to all 0 (not necessary for normals and vertex indices) */
        memset(verts,0,nVerts*3*sizeof(GLfloat));

        /* pregen Berstein polynomials and their first derivatives (for normals) */
        if (!*inited)
            pregenBernstein(nSubDivs,bern_0,bern_1);

        /* generate vertices and normals */
        for (p=0, o=0; p<GLUT_TEAPOT_N_INPUT_PATCHES; p++)
        {
            /* set flags for evalBezier function */
            int flag      = p<6?4:2;            /* first six patches get 3 copies (rotations), last four get 2 copies (flips) */
            int normalFix = p==3?1:p==5?2:0;    /* Fix normal vectors for vertices on top of lid (patch 4) and on middle of bottom (patch 6). Different flag value as different normal needed */

            /* collect control points */
            int i;
            for (i=0; i<16; i++)
            {
                /* Original code draw with a 270� rot around X axis, a scaling and a translation along the Z-axis.
                 * Incorporating these in the control points is much cheaper than transforming all the vertices.
                 * Original:
                 * glRotated( 270.0, 1.0, 0.0, 0.0 );
                 * glScaled( 0.5 * scale, 0.5 * scale, 0.5 * scale );
                 * glTranslated( 0.0, 0.0, -1.5 );
                 */
                cp[i/4][i%4][0] =  cpdata[patchdata[p][i]][0]      *scale/2.f;
                cp[i/4][i%4][1] = (cpdata[patchdata[p][i]][2]-1.5f)*scale/2.f;
                cp[i/4][i%4][2] = -cpdata[patchdata[p][i]][1]      *scale/2.f;
            }

            /* eval bezier patch */
            if (!*inited)   /* first time, generate normals as well */
                o += evalBezierWithNorm(cp,nSubDivs,bern_0,bern_1, flag, normalFix, verts+o,norms+o);
            else            /* only need to regen vertices */
                o += evalBezier(cp,nSubDivs,bern_0, flag, verts+o);
        }
        *lastScale = scale;

        if (!*inited)
        {
            int r,c;
            /* generate texture coordinates if solid teapot */
            if (!useWireMode)
            {
                /* generate for first patch */
                for (r=0,o=0; r<nSubDivs; r++)
                {
                    GLfloat u = r/(nSubDivs-1.f);
                    for (c=0; c<nSubDivs; c++, o+=2)
                    {
                        GLfloat v = c/(nSubDivs-1.f);
                        texcsSolid[o+0] = u;
                        texcsSolid[o+1] = v;
                    }
                }
                /* copy it over for all the other patches */
                for (p=1; p<GLUT_TEAPOT_N_PATCHES; p++)
                    memcpy(texcsSolid+p*nSubDivs*nSubDivs*2,texcsSolid,nSubDivs*nSubDivs*2*sizeof(GLfloat));
            }

            /* build vertex index array */
            if (useWireMode)
            {
                /* build vertex indices to draw teapot as line strips */
                /* first strips along increasing u, constant v */
                for (p=0, o=0; p<GLUT_TEAPOT_N_PATCHES; p++)
                {
                    int idx = nSubDivs*nSubDivs*p;
                    for (c=0; c<nSubDivs; c++)
                        for (r=0; r<nSubDivs; r++, o++)
                            vertIdxs[o] = idx+r*nSubDivs+c;
                }

                /* then strips along increasing v, constant u */
                for (p=0; p<GLUT_TEAPOT_N_PATCHES; p++) /* don't reset o, we continue appending! */
                {
                    int idx = nSubDivs*nSubDivs*p;
                    for (r=0; r<nSubDivs; r++)
                    {
                        int loc = r*nSubDivs;
                        for (c=0; c<nSubDivs; c++, o++)
                            vertIdxs[o] = idx+loc+c;
                    }
                }
            }
            else
            {
                /* build vertex indices to draw teapot as triangles */
                for (p=0,o=0; p<GLUT_TEAPOT_N_PATCHES; p++)
                {
                    int idx = nSubDivs*nSubDivs*p;
                    for (r=0; r<nSubDivs-1; r++)
                    {
                        int loc = r*nSubDivs;
                        for (c=0; c<nSubDivs-1; c++, o+=6)
                        {
                            /* ABC ACD, where B and C are one row lower */
                            int row1 = idx+loc+c;
                            int row2 = row1+nSubDivs;

                            vertIdxs[o+0] = row1+0;
                            vertIdxs[o+1] = row2+0;
                            vertIdxs[o+2] = row2+1;

                            vertIdxs[o+3] = row1+0;
                            vertIdxs[o+4] = row2+1;
                            vertIdxs[o+5] = row1+1;
                        }
                    }
                }
            }

            *inited = GL_TRUE;
        }
    }

    /* draw */
    // TODO: texture coordinates
    if (useWireMode)
        fghDrawGeometryWire(verts, norms, nVerts, vertIdxs, GLUT_TEAPOT_N_PATCHES*nSubDivs*2, nSubDivs, GL_LINE_STRIP, NULL,0,0);
    else
        fghDrawGeometrySolid(verts,norms,texcsSolid,nVerts,vertIdxs,1,GLUT_SOLID_TEAPOT_N_TRI*3);
}


/* -- INTERFACE FUNCTIONS -------------------------------------------------- */

/*
 * Renders a wired teapot...
 */
void FGAPIENTRY glutWireTeapot( double size )
{
    FREEGLUT_EXIT_IF_NOT_INITIALISED ( "glutWireTeapot" );
    /* We will use the general teapot rendering code */
    fghTeapot( (GLfloat)size, GL_TRUE );
}

/*
 * Renders a filled teapot...
 */
void FGAPIENTRY glutSolidTeapot( double size )
{
    FREEGLUT_EXIT_IF_NOT_INITIALISED ( "glutSolidTeapot" );
    /* We will use the general teapot rendering code */
    fghTeapot( (GLfloat)size, GL_FALSE );
}

/*** END OF FILE ***/





