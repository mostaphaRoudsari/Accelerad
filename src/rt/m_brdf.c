/* Copyright (c) 1991 Regents of the University of California */

#ifndef lint
static char SCCSid[] = "$SunId$ LBL";
#endif

/*
 *  Shading for materials with arbitrary BRDF's
 */

#include  "ray.h"

#include  "data.h"

#include  "otypes.h"

/*
 *	Arguments to this material include the color and specularity.
 *  String arguments include the reflection function and files.
 *  The BRDF is currently used just for the specular component to light
 *  sources.  Reflectance values or data coordinates are functions
 *  of the direction to the light source.
 *	We orient the surface towards the incoming ray, so a single
 *  surface can be used to represent an infinitely thin object.
 *
 *  Arguments for MAT_PFUNC and MAT_MFUNC are:
 *	2+	func	funcfile	transform
 *	0
 *	4+	red	grn	blu	specularity	A5 ..
 *
 *  Arguments for MAT_PDATA and MAT_MDATA are:
 *	4+	func	datafile	funcfile	v0 ..	transform
 *	0
 *	4+	red	grn	blu	specularity	A5 ..
 *
 *  Arguments for MAT_TFUNC are:
 *	2+	func	funcfile	transform
 *	0
 *	4+	red	grn	blu	rspec	trans	tspec	A7 ..
 *
 *  Arguments for MAT_TDATA are:
 *	4+	func	datafile	funcfile	v0 ..	transform
 *	0
 *	4+	red	grn	blu	rspec	trans	tspec	A7 ..
 *
 *  Arguments for the more general MAT_BRTDF are:
 *	10+	rrefl	grefl	brefl
 *		rtrns	gtrns	btrns
 *		rbrtd	gbrtd	bbrtd
 *		funcfile	transform
 *	0
 *	6+	red	grn	blu	rspec	trans	tspec	A7 ..
 *
 *	In addition to the normal variables available to functions,
 *  we define the following:
 *		NxP, NyP, NzP -		perturbed surface normal
 *		RdotP -			perturbed ray dot product
 *		CrP, CgP, CbP -		perturbed material color
 */

extern double	funvalue(), varvalue();
extern XF  funcxf;

typedef struct {
	OBJREC  *mp;		/* material pointer */
	RAY  *pr;		/* intersected ray */
	DATARRAY  *dp;		/* data array for PDATA, MDATA or TDATA */
	COLOR  mcolor;		/* color of this material */
	double  rspec;		/* specular reflection */
	double	rdiff;		/* diffuse reflection */
	double  trans;		/* transmissivity */
	double  tspec;		/* specular transmission */
	double  tdiff;		/* diffuse transmission */
	FVECT  pnorm;		/* perturbed surface normal */
	double  pdot;		/* perturbed dot product */
}  BRDFDAT;		/* BRDF material data */


dirbrdf(cval, np, ldir, omega)		/* compute source contribution */
COLOR  cval;			/* returned coefficient */
register BRDFDAT  *np;		/* material data */
FVECT  ldir;			/* light source direction */
double  omega;			/* light source size */
{
	double  ldot;
	double  dtmp;
	COLOR  ctmp;
	FVECT  ldx;
	double  pt[MAXDIM];
	register char	**sa;
	register int	i;

	setcolor(cval, 0.0, 0.0, 0.0);
	
	ldot = DOT(np->pnorm, ldir);

	if (ldot <= FTINY && ldot >= -FTINY)
		return;		/* too close to grazing */
	if (ldot < 0.0 ? np->trans <= FTINY : np->trans >= 1.0-FTINY)
		return;		/* wrong side */

	if (ldot > 0.0 && np->rdiff > FTINY) {
		/*
		 *  Compute and add diffuse reflected component to returned
		 *  color.  The diffuse reflected component will always be
		 *  modified by the color of the material.
		 */
		copycolor(ctmp, np->mcolor);
		dtmp = ldot * omega * np->rdiff / PI;
		scalecolor(ctmp, dtmp);
		addcolor(cval, ctmp);
	}
	if (ldot < 0.0 && np->tdiff > FTINY) {
		/*
		 *  Diffuse transmitted component.
		 */
		copycolor(ctmp, np->mcolor);
		dtmp = -ldot * omega * np->tdiff / PI;
		scalecolor(ctmp, dtmp);
		addcolor(cval, ctmp);
	}
	if (ldot > 0.0 ? np->rspec <= FTINY : np->tspec <= FTINY)
		return;		/* no specular component */
					/* set up function */
	setfunc(np->mp, np->pr);
	sa = np->mp->oargs.sarg;
	errno = 0;
					/* transform light vector */
	multv3(ldx, ldir, funcxf.xfm);
	for (i = 0; i < 3; i++)
		ldx[i] /= funcxf.sca;
					/* compute BRTDF */
	if (np->mp->otype == MAT_BRTDF) {
		colval(ctmp,RED) = funvalue(sa[6], 3, ldx);
		if (!strcmp(sa[7],sa[6]))
			colval(ctmp,GRN) = colval(ctmp,RED);
		else
			colval(ctmp,GRN) = funvalue(sa[7], 3, ldx);
		if (!strcmp(sa[8],sa[6]))
			colval(ctmp,BLU) = colval(ctmp,RED);
		else if (!strcmp(sa[8],sa[7]))
			colval(ctmp,BLU) = colval(ctmp,GRN);
		else
			colval(ctmp,BLU) = funvalue(sa[8], 3, ldx);
		dtmp = bright(ctmp);
	} else if (np->dp == NULL) {
		dtmp = funvalue(sa[0], 3, ldx);
		setcolor(ctmp, dtmp, dtmp, dtmp);
	} else {
		for (i = 0; i < np->dp->nd; i++)
			pt[i] = funvalue(sa[3+i], 3, ldx);
		dtmp = datavalue(np->dp, pt);
		dtmp = funvalue(sa[0], 1, &dtmp);
		setcolor(ctmp, dtmp, dtmp, dtmp);
	}
	if (errno)
		goto computerr;
	if (dtmp <= FTINY)
		return;
	if (ldot > 0.0) {
		/*
		 *  Compute reflected non-diffuse component.
		 */
		if (np->mp->otype == MAT_MFUNC || np->mp->otype == MAT_MDATA)
			multcolor(ctmp, np->mcolor);
		dtmp = ldot * omega * np->rspec;
		scalecolor(ctmp, dtmp);
		addcolor(cval, ctmp);
	} else {
		/*
		 *  Compute transmitted non-diffuse component.
		 */
		if (np->mp->otype == MAT_TFUNC || np->mp->otype == MAT_TDATA)
			multcolor(ctmp, np->mcolor);
		dtmp = -ldot * omega * np->tspec;
		scalecolor(ctmp, dtmp);
		addcolor(cval, ctmp);
	}
	return;
computerr:
	objerror(np->mp, WARNING, "compute error");
	return;
}


m_brdf(m, r)			/* color a ray which hit a BRDF material */
register OBJREC  *m;
register RAY  *r;
{
	int  minsa, minfa;
	BRDFDAT  nd;
	double  transtest, transdist;
	COLOR  ctmp;
	double  dtmp;
	FVECT  vec;
	register int  i;
						/* check arguments */
	switch (m->otype) {
	case MAT_PFUNC: case MAT_MFUNC:
		minsa = 2; minfa = 4; break;
	case MAT_PDATA: case MAT_MDATA:
		minsa = 4; minfa = 4; break;
	case MAT_TFUNC:
		minsa = 2; minfa = 6; break;
	case MAT_TDATA:
		minsa = 4; minfa = 6; break;
	case MAT_BRTDF:
		minsa = 10; minfa = 6; break;
	}
	if (m->oargs.nsargs < minsa || m->oargs.nfargs < minfa)
		objerror(m, USER, "bad # arguments");
	nd.mp = m;
	nd.pr = r;
						/* get specular component */
	nd.rspec = m->oargs.farg[3];
						/* compute transmission */
	if (m->otype == MAT_TFUNC || m->otype == MAT_TDATA
			|| m->otype == MAT_BRTDF) {
		nd.trans = m->oargs.farg[4]*(1.0 - nd.rspec);
		nd.tspec = nd.trans * m->oargs.farg[5];
		nd.tdiff = nd.trans - nd.tspec;
	} else
		nd.tdiff = nd.tspec = nd.trans = 0.0;
						/* early shadow check */
	if (r->crtype & SHADOW && (m->otype != MAT_BRTDF || nd.tspec <= FTINY))
		return;
						/* diffuse reflection */
	nd.rdiff = 1.0 - nd.trans - nd.rspec;
						/* get material color */
	setcolor(nd.mcolor, m->oargs.farg[0],
			   m->oargs.farg[1],
			   m->oargs.farg[2]);
						/* fix orientation */
	if (r->rod < 0.0)
		flipsurface(r);
						/* get modifiers */
	raytexture(r, m->omod);
	nd.pdot = raynormal(nd.pnorm, r);	/* perturb normal */
	multcolor(nd.mcolor, r->pcol);		/* modify material color */
	r->rt = r->rot;				/* default ray length */
	transtest = 0;
						/* load auxiliary files */
	if (m->otype == MAT_PDATA || m->otype == MAT_MDATA
			|| m->otype == MAT_TDATA) {
		nd.dp = getdata(m->oargs.sarg[1]);
		for (i = 3; i < m->oargs.nsargs; i++)
			if (m->oargs.sarg[i][0] == '-')
				break;
		if (i-3 != nd.dp->nd)
			objerror(m, USER, "dimension error");
		if (!fundefined(m->oargs.sarg[3]))
			loadfunc(m->oargs.sarg[2]);
	} else if (m->otype == MAT_BRTDF) {
		nd.dp = NULL;
		if (!fundefined(m->oargs.sarg[7]))
			loadfunc(m->oargs.sarg[9]);
	} else {
		nd.dp = NULL;
		if (!fundefined(m->oargs.sarg[0]))
			loadfunc(m->oargs.sarg[1]);
	}
						/* set special variables */
	setfunc(m, r);
	multv3(vec, nd.pnorm, funcxf.xfm);
	varset("NxP", '=', vec[0]/funcxf.sca);
	varset("NyP", '=', vec[1]/funcxf.sca);
	varset("NzP", '=', vec[2]/funcxf.sca);
	varset("RdotP", '=', nd.pdot);
	varset("CrP", '=', colval(nd.mcolor,RED));
	varset("CgP", '=', colval(nd.mcolor,GRN));
	varset("CbP", '=', colval(nd.mcolor,BLU));
						/* compute transmitted ray */
	if (m->otype == MAT_BRTDF && nd.tspec > FTINY) {
		RAY  sr;
		errno = 0;
		setcolor(ctmp, varvalue(m->oargs.sarg[0]),
				varvalue(m->oargs.sarg[1]),
				varvalue(m->oargs.sarg[2]));
		scalecolor(ctmp, nd.tspec);
		if (errno)
			objerror(m, WARNING, "compute error");
		else if ((dtmp = bright(ctmp)) > FTINY &&
				rayorigin(&sr, r, TRANS, dtmp) == 0) {
			if (DOT(r->pert,r->pert) > FTINY*FTINY) {
				for (i = 0; i < 3; i++)	/* perturb direction */
					sr.rdir[i] = r->rdir[i] -
							.75*r->pert[i];
				normalize(sr.rdir);
			} else {
				VCOPY(sr.rdir, r->rdir);
				transtest = 2;
			}
			rayvalue(&sr);
			multcolor(sr.rcol, ctmp);
			addcolor(r->rcol, sr.rcol);
			transtest *= bright(sr.rcol);
			transdist = r->rot + sr.rt;
		}
	}
	if (r->crtype & SHADOW)			/* the rest is shadow */
		return;
						/* compute reflected ray */
	if (m->otype == MAT_BRTDF && nd.rspec > FTINY) {
		RAY  sr;
		errno = 0;
		setcolor(ctmp, varvalue(m->oargs.sarg[3]),
				varvalue(m->oargs.sarg[4]),
				varvalue(m->oargs.sarg[5]));
		scalecolor(ctmp, nd.rspec);
		if (errno)
			objerror(m, WARNING, "compute error");
		else if ((dtmp = bright(ctmp)) > FTINY &&
				rayorigin(&sr, r, REFLECTED, dtmp) == 0) {
			for (i = 0; i < 3; i++)
				sr.rdir[i] = r->rdir[i] +
						2.0*nd.pdot*nd.pnorm[i];
			rayvalue(&sr);
			multcolor(sr.rcol, ctmp);
			addcolor(r->rcol, sr.rcol);
		}
	}
						/* compute ambient */
	if (nd.rdiff > FTINY) {
		ambient(ctmp, r);
		if (m->otype == MAT_BRTDF)
			scalecolor(ctmp, nd.rdiff);
		else
			scalecolor(ctmp, 1.0-nd.trans);
		multcolor(ctmp, nd.mcolor);	/* modified by material color */
		addcolor(r->rcol, ctmp);	/* add to returned color */
	}
	if (nd.tdiff > FTINY) {			/* from other side */
		flipsurface(r);
		ambient(ctmp, r);
		if (m->otype == MAT_BRTDF)
			scalecolor(ctmp, nd.tdiff);
		else
			scalecolor(ctmp, nd.trans);
		multcolor(ctmp, nd.mcolor);
		addcolor(r->rcol, ctmp);
		flipsurface(r);
	}
						/* add direct component */
	direct(r, dirbrdf, &nd);
						/* check distance */
	if (transtest > bright(r->rcol))
		r->rt = transdist;
}
