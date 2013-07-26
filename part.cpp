// ***** BEGIN LICENSE BLOCK *****
// Version: MPL 1.1
//
// The contents of this file are subject to the Mozilla Public License Version
// 1.1 (the "License"); you may not use this file except in compliance with
// the License. You may obtain a copy of the License at
// http://www.mozilla.org/MPL/
//
// Software distributed under the License is distributed on an "AS IS" basis,
// WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
// for the specific language governing rights and limitations under the
// License.
//
// The Original Code is part.cpp.
//
// The Initial Developer of the Original Code is Eckhart K�ppen.
// Copyright (C) 2004 the Initial Developer. All Rights Reserved.
//
// ***** END LICENSE BLOCK *****

// ANSI C & POSIX
#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>

// NEWT/0
#include <NewtCore.h>
#include <NewtVM.h>
#include <NewtParser.h>
#include <NewtNSOF.h>
#include <NewtBC.h>
#include <NewtGC.h>
#include <NewtPkg.h>
#include <NewtPrint.h>

#include "part.h"

static void hex_dump(unsigned char *data, int size);

TPart::TPart (newtRefVar partInfo):
    fData (NULL),
    fDataLen (0),
    fType (kForm),
    fConstants (kNewtRefNIL),
    fGlobalFunctions (kNewtRefNIL),
    fFiles (NULL),
    fNumFiles (0)
{
    newtRefVar files, map;
    int i;

    fMainFile = strdup (NewtRefToString (NcGetSlot (partInfo, NewtMakeSymbol ("main"))));
    files = NcGetSlot (partInfo, NewtMakeSymbol ("files"));
    if (files != kNewtRefNIL) {
	fNumFiles = NewtArrayLength (files);
	fFiles = (char **) malloc (fNumFiles * sizeof (char *));
	for (i = 0; i < fNumFiles; i++) {
	    fFiles[i] = strdup (NewtRefToString (NewtGetArraySlot (files, i)));
	}
    }
    if (NcGetSlot (partInfo, NewtMakeSymbol ("type")) == NewtMakeSymbol ("auto")) {
	fType = kAuto;
    }
}

TPart::~TPart ()
{
    while (fNumFiles-- > 0) {
	delete fFiles[fNumFiles];
    }
    delete fFiles;
    delete fMainFile;
}

newtRef TPart::MInterpretFile (const char *f)
{
    nps_syntax_node_t *stree = NULL;
    uint32_t numStree = 0;
    newtRefVar fn = kNewtRefNIL;
    newtRefVar constants;
    newtErr err;
    newtRefVar map, s, v;
    int i;

    printf ("Compiling %s\n", f);
    err = NPSParseFile (f, &stree, &numStree);

    NBCInit ();
    constants = NBCConstantTable ();
    if (fConstants != kNewtRefNIL) {
	map = NewtFrameMap (fConstants);
	for (i = 0; i < NewtMapLength (map); i++) {
	    s = NewtGetArraySlot (map, i);
	    v = NcGetSlot (fConstants, s);
	    NcSetSlot (constants, s, v);
	}
    }

    fn = NBCGenBC (stree, numStree, true);
    NBCCleanup ();
    fn = NVMCall (fn, 0, &err);
    NPSCleanup ();
    return fn;
}

void TPart::MReadPlatformFile (const char *fileName)
{
    FILE *f;
    uint8_t *buffer;
    struct stat st;
    newtRefVar pf;

    f = fopen (fileName, "rb");
    if (f != NULL) {
	printf ("Reading platform file %s\n", fileName);
	fstat (fileno (f), &st);
	buffer = (uint8_t *) malloc (st.st_size);
	fread (buffer, st.st_size, 1, f);
	pf = NewtReadNSOF (buffer, st.st_size);

	fConstants = NcGetSlot (pf, NewtMakeSymbol ("platformConstants"));
	fGlobalFunctions = NcGetSlot (pf, NewtMakeSymbol ("platformFunctions"));

	free (buffer);
	fclose (f);
    }
}

void TPart::MBuildPart (const char *platformFileName)
{
    newtRefVar packageNSOF;
    newtObjRef obj;
    char* data;
    int len;
    int i;

    NVMInit ();
    for (i = 0; i < fNumFiles; i++) {
	MReadPlatformFile (platformFileName);
	MInterpretFile (fFiles[i]);
    }
    MReadPlatformFile (platformFileName);
    fMainForm = MInterpretFile (fMainFile);

    packageNSOF = NsMakeNSOF (kNewtRefNIL, fMainForm, NewtMakeInt30 (2));
    obj = NewtRefToPointer (packageNSOF);

    fDataLen = NewtObjSize (obj);
    fData = (unsigned char *) malloc (fDataLen);
    memcpy (fData, NewtObjData (obj), fDataLen);

    NVMClean ();
}

void TPart::MDump ()
{
    hex_dump (fData, fDataLen);
}

static void hex_dump(unsigned char *data, int size)
{
    /* dumps size bytes of *data to stdout. Looks like:
     * [0000] 75 6E 6B 6E 6F 77 6E 20
     *                  30 FF 00 00 00 00 39 00 unknown 0.....9.
     * (in a single line of course)
     */

    unsigned char *p = (unsigned char *) data;
    unsigned char c;
    int n;
    char bytestr[4] = {0};
    char addrstr[10] = {0};
    char hexstr[ 16*3 + 5] = {0};
    char charstr[16*1 + 5] = {0};
    for(n=1;n<=size;n++) {
	if (n%16 == 1) {
	    /* store address for this line */
	    snprintf(addrstr, sizeof(addrstr), "%.4x",
		     (unsigned int)(p-data) );
	}

	c = *p;
	if (isalnum(c) == 0) {
	    c = '.';
	}

	/* store hex str (for left side) */
	snprintf(bytestr, sizeof(bytestr), "%02X ", *p);
	strncat(hexstr, bytestr, sizeof(hexstr)-strlen(hexstr)-1);

	/* store char str (for right side) */
	snprintf(bytestr, sizeof(bytestr), "%c", c);
	strncat(charstr, bytestr, sizeof(charstr)-strlen(charstr)-1);

	if(n%16 == 0) { 
	    /* line completed */
	    printf("[%4.4s]   %-50.50s  %s\n", addrstr, hexstr, charstr);
	    hexstr[0] = 0;
	    charstr[0] = 0;
	} else if(n%8 == 0) {
	    /* half line: add whitespaces */
	    strncat(hexstr, "  ", sizeof(hexstr)-strlen(hexstr)-1);
	    strncat(charstr, " ", sizeof(charstr)-strlen(charstr)-1);
	}
	p++; /* next byte */
    }

    if (strlen(hexstr) > 0) {
	/* print rest of buffer if not empty */
	printf("[%4.4s]   %-50.50s  %s\n", addrstr, hexstr, charstr);
    }
}
