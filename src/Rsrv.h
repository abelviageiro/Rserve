/*
 *  Rsrv.h : constants and macros for Rsrv client/server architecture
 *  Copyright (C) 2002 Simon Urbanek
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  $Id$
 */

#ifndef __RSRV_H__
#define __RSRV_H__

#define default_Rsrv_port 6311

struct phdr { /* always 16 bytes */
  int cmd; /* command */
  int len; /* length of the packet minus header (ergo -16) */
  int dof; /* data offset behind header (ergo usually 0) */
  int res; /* reserved - but must be sent so the minimal packet has 16 bytes */
};

/* each parameter is preceded by 4 bytes:
   1 byte : parameter type
   3 bytes: length
   parameter list may be terminated by 0/0/0/0 but doesn't have to singe "len"
   field specifies the packet length sufficiently (hint: best method for parsing is
   to allocate len+4 bytes, set the last 4 bytes to 0 and trverse list of parameters
   until (int)0 occurs */

#define PAR_TYPE(X) ((X)&255)
#define PAR_LEN(X) ((X)>>8)
#define PAR_LENGTH PAR_LEN
#define SET_PAR(TY,LEN) ((((LEN)&0x7fffff)<<8)|((TY)&255))

#define CMD_RESP 0x10000  /* all responses have this flag set */
#define CMD_STAT(X) (((X)>>24)&127) /* returns the stat code of the response */
#define SET_STAT(X,s) ((X)|(((s)&127)<<24)) /* sets the stat code */

#define RESP_OK (CMD_RESP|0x0001) /* command succeeded; returned parameters depend
				     on the command issued */
#define RESP_ERR (CMD_RESP|0x0002) /* command failed, check stats code
				      attached string may describe the error */

#define ERR_auth_failed      0x41 /* auth.failed or auth.reqeusted but no login came */
#define ERR_conn_broken      0x42 /* connection closed or broken packet killed it */
#define ERR_inv_cmd          0x43 /* unsupported/invalid command */
#define ERR_inv_par          0x44 /* some pars are invalid */

#define CMD_login    0x001 /* name, pwd : - */
#define CMD_voidEval 0x002 /* string : - */
#define CMD_eval     0x003 /* string : encoded SEXP */
#define CMD_shutdown 0x004 /* [admin-pwd] : - */

#define DT_INT        1  /* int */
#define DT_CHAR       2  /* char */
#define DT_DOUBLE     3  /* double */
#define DT_STRING     4  /* 0 terminted string */
#define DT_BYTESTREAM 5  /* stream of bytes (unlike DT_STRING may contain 0) */
#define DT_SEXP       10 /* encoded SEXP */
#define DT_ARRAY      11 /* array of objects (i.e. first 4 bytes specify how many
			    subsequent objects are part of the array; 0 is legitimate) */

#define XT_NULL          0
#define XT_INT           1
#define XT_DOUBLE        2
#define XT_STR           3 /* null-term. strg. */
#define XT_LANG          4 /* lang XP */
#define XT_SYM           5 /* symbol */

#define XT_VECTOR        16 /* of any Xs */
#define XT_LIST          17 /* X head, X vals */

#define XT_ARRAY_INT     32
#define XT_ARRAY_DOUBLE  33
#define XT_ARRAY_STR     34

#define XT_UNKNOWN       48

#define XT_HAS_ATTR      128

#define GET_XT(X) ((X)&127)
#define HAS_ATTR(X) (((X)&XT_HAS_ATTR)>0)

/* ID string (sent by server on connect) must be 32 bytes long and consists of:
   "Rsrv" - R-server ID signature
   "0100" - version of the R server
   "QAP1" - protocol used for communication (here Quad Attributes Packets v1)
   any additional attributes follow. \r\n<space> and '-' are ignored.

   optional attributes
   (in any order; it is legitimate to put "----" or spaces between attributes):
   "R151" - version of R (here 1.5.1)
   "ARpt" - authorization required (here "pt"=plain text) connection will be closed
            if the first packet is not CMD_login
   "K***" - key if encoded authentification is challenged (*** is the key) */


#endif
