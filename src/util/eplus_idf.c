#ifndef lint
static const char RCSid[] = "$Id: eplus_idf.c,v 2.1 2014/02/01 01:28:43 greg Exp $";
#endif
/*
 *  eplus_idf.c
 *
 *  EnergyPlus Input Data File i/o routines
 *
 *  Created by Greg Ward on 1/31/14.
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "eplus_idf.h"

#ifdef getc_unlocked            /* avoid horrendous overhead of flockfile */
#undef getc
#define getc    getc_unlocked
#endif

/* Create a new parameter with empty field list (comment optional) */
IDF_PARAMETER *
idf_newparam(IDF_LOADED *idf, const char *pname, const char *comm,
			IDF_PARAMETER *prev)
{
	LUENT		*pent;
	IDF_PARAMETER	*pnew;

	if ((idf == NULL) | (pname == NULL))
		return(NULL);
	if (comm == NULL) comm = "";
	pent = lu_find(&idf->ptab, pname);
	if (pent == NULL)
		return(NULL);
	if (pent->key == NULL) {	/* new parameter name/type? */
		pent->key = (char *)malloc(strlen(pname)+1);
		if (pent->key == NULL)
			return(NULL);
		strcpy(pent->key, pname);
	}
	pnew = (IDF_PARAMETER *)malloc(sizeof(IDF_PARAMETER)+strlen(comm));
	if (pnew == NULL)
		return(NULL);
	strcpy(pnew->rem, comm);
	pnew->flist = NULL;
	pnew->pname = pent->key;	/* add to table */
	pnew->pnext = (IDF_PARAMETER *)pent->data;
	pent->data = (char *)pnew;
	pnew->dnext = NULL;		/* add to file list */
	if (prev != NULL || (prev = idf->plast) != NULL) {
		pnew->dnext = prev->dnext;
		if (prev == idf->plast)
			idf->plast = pnew;
	}
	if (idf->pfirst == NULL)
		idf->pfirst = idf->plast = pnew;
	else
		prev->dnext = pnew;
	return(pnew);
}

/* Add a field to the given parameter and follow with the given text */
int
idf_addfield(IDF_PARAMETER *param, const char *fval, const char *comm)
{
	int		fnum = 1;	/* returned argument number */
	IDF_FIELD	*fnew, *flast;
	char		*cp;

	if ((param == NULL) | (fval == NULL))
		return(0);
	if (comm == NULL) comm = "";
	fnew = (IDF_FIELD *)malloc(sizeof(IDF_FIELD)+strlen(fval)+strlen(comm));
	if (fnew == NULL)
		return(0);
	fnew->next = NULL;
	cp = fnew->arg;			/* copy argument and comments */
	while ((*cp++ = *fval++))
		;
	fnew->rem = cp;
	while ((*cp++ = *comm++))
		;
					/* add to parameter's field list */
	if ((flast = param->flist) != NULL) {
		++fnum;
		while (flast->next != NULL) {
			flast = flast->next;
			++fnum;
		}
	}
	if (flast == NULL)
		param->flist = fnew;
	else
		flast->next = fnew;
	return(fnum);
}

/* Delete the specified parameter from loaded IDF */
int
idf_delparam(IDF_LOADED *idf, IDF_PARAMETER *param)
{
	LUENT		*pent;
	IDF_PARAMETER	*pptr, *plast;

	if ((idf == NULL) | (param == NULL))
		return(0);
					/* remove from parameter table */
	pent = lu_find(&idf->ptab, param->pname);
	for (plast = NULL, pptr = (IDF_PARAMETER *)pent->data;
				pptr != NULL; plast = pptr, pptr = pptr->pnext)
		if (pptr == param)
			break;
	if (pptr == NULL)
		return(0);
	if (plast == NULL)
		pent->data = (char *)param->pnext;
	else
		plast->pnext = param->pnext;
					/* remove from global list */
	for (plast = NULL, pptr = idf->pfirst;
				pptr != NULL; plast = pptr, pptr = pptr->dnext)
		if (pptr == param)
			break;
	if (pptr == NULL)
		return(0);
	if (plast == NULL)
		idf->pfirst = param->dnext;
	else
		plast->dnext = param->dnext;
	if (idf->plast == param)
		idf->plast = plast;
					/* free field list */
	while (param->flist != NULL) {
		IDF_FIELD	*fdel = param->flist;
		param->flist = fdel->next;
		free(fdel);
	}
	free(param);			/* free parameter struct */
	return(1);
}

/* Get a named parameter list */
IDF_PARAMETER *
idf_getparam(IDF_LOADED *idf, const char *pname)
{
	if ((idf == NULL) | (pname == NULL))
		return(NULL);

	return((IDF_PARAMETER *)lu_find(&idf->ptab,pname)->data);
}

/* Read an argument including terminating ',' or ';' -- return which */
static int
idf_read_argument(char *buf, FILE *fp, int trim)
{
	int	skipwhite = trim;
	char	*cp = buf;
	int	c;

	while ((c = getc(fp)) != EOF && (c != ',') & (c != ';')) {
		if (skipwhite && isspace(c))
			continue;
		skipwhite = 0;
		if (cp-buf < IDF_MAXLINE-1)
			*cp++ = c;
	}
	if (trim)
		while (cp > buf && isspace(cp[-1]))
			--cp;
	*cp = '\0';
	return(c);
}

/* Read a comment, including all white space up to next alpha character */
static void
idf_read_comment(char *buf, int len, FILE *fp)
{
	int	incomm = 0;
	char	*cp = buf;
	char	dummyc;
	int	c;

	if ((buf == NULL) | (len <= 0)) {
		buf = &dummyc;
		len = 1;
	}
	while ((c = getc(fp)) != EOF && isspace(c) | incomm) {
		if (c == '!')
			++incomm;
		else if (c == '\n')
			incomm = 0;
		if (cp-buf < len-1)
			*cp++ = c;
	}
	*cp = '\0';
	if (c != EOF)
		ungetc(c, fp);
}

/* Read a parameter and fields from an open file and add to end of list */
IDF_PARAMETER *
idf_readparam(IDF_LOADED *idf, FILE *fp)
{
	char		abuf[IDF_MAXLINE], cbuf[IDF_MAXLINE];
	int		delim;
	IDF_PARAMETER	*pnew;
	
	if ((delim = idf_read_argument(abuf, fp, 1)) == EOF)
		return(NULL);
	idf_read_comment(cbuf, IDF_MAXLINE, fp);
	pnew = idf_newparam(idf, abuf, cbuf, NULL);
	while (delim == ',')
		if ((delim = idf_read_argument(abuf, fp, 1)) != EOF) {
			idf_read_comment(cbuf, IDF_MAXLINE, fp);
			idf_addfield(pnew, abuf, cbuf);
		}
	if (delim != ';')
		fprintf(stderr, "Expected ';' at end of parameter list\n");
	return(pnew);
}

/* Initialize an IDF struct */
IDF_LOADED *
idf_create(const char *hdrcomm)
{
	IDF_LOADED	*idf = (IDF_LOADED *)calloc(1, sizeof(IDF_LOADED));

	if (idf == NULL)
		return(NULL);
	idf->ptab.hashf = &lu_shash;
	idf->ptab.keycmp = &strcmp;
	idf->ptab.freek = &free;
	lu_init(&idf->ptab, 200);
	if (hdrcomm != NULL && *hdrcomm) {
		idf->hrem = (char *)malloc(strlen(hdrcomm)+1);
		if (idf->hrem != NULL)
			strcpy(idf->hrem, hdrcomm);
	}
	return(idf);
}

/* Load an Input Data File */
IDF_LOADED *
idf_load(const char *fname)
{
	char		*hdrcomm;
	FILE		*fp;
	IDF_LOADED	*idf;

	if (fname == NULL)
		fp = stdin;		/* open file if not stdin */
	else if ((fp = fopen(fname, "r")) == NULL)
		return(NULL);
					/* read header comments */
	hdrcomm = (char *)malloc(100*IDF_MAXLINE);
	idf_read_comment(hdrcomm, 100*IDF_MAXLINE, fp);
	idf = idf_create(hdrcomm);	/* create IDF struct */
	free(hdrcomm);
	if (idf == NULL)
		return(NULL);
					/* read each parameter */
	while (idf_readparam(idf, fp) != NULL)
		;
	if (fp != stdin)		/* close file if not stdin */
		fclose(fp);
	return(idf);			/* success! */
}

/* Write a parameter and fields to an open file */
int
idf_writeparam(IDF_PARAMETER *param, FILE *fp)
{
	IDF_FIELD	*fptr;

	if ((param == NULL) | (fp == NULL))
		return(0);
	fputs(param->pname, fp);
	fputc(',', fp);
	fputs(param->rem, fp);
	for (fptr = param->flist; fptr != NULL; fptr = fptr->next) {
		fputs(fptr->arg, fp);
		fputc((fptr->next==NULL ? ';' : ','), fp);
		fputs(fptr->rem, fp);
	}
	return(!ferror(fp));
}

/* Write out an Input Data File */
int
idf_write(IDF_LOADED *idf, const char *fname)
{
	FILE		*fp;
	IDF_PARAMETER	*pptr;

	if (idf == NULL)
		return(0);
	if (fname == NULL)
		fp = stdout;		/* open file if not stdout */
	else if ((fp = fopen(fname, "w")) == NULL)
		return(0);
	fputs(idf->hrem, fp);		/* write header then parameters */
	for (pptr = idf->pfirst; pptr != NULL; pptr = pptr->dnext)
		if (!idf_writeparam(pptr, fp)) {
			fclose(fp);
			return(0);
		}
	if (fp == stdout)		/* flush/close file & check status */
		return(fflush(fp) == 0);
	return(fclose(fp) == 0);
}

/* Free a loaded IDF */
void
idf_free(IDF_LOADED *idf)
{
	if (idf == NULL)
		return;
	if (idf->hrem != NULL)
		free(idf->hrem);
	while (idf->pfirst != NULL)
		idf_delparam(idf, idf->pfirst);
	lu_done(&idf->ptab);
}
