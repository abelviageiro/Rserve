/*
 *  Rserv : R-server that allows to use embedded R via TCP/IP
 *          currently based on R-1.5.1 API
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

/* external defines: (for unix platfoms: FORKED is highly recommended!)

   THREADED   - results in threaded version of this server, i.e. each
                new connection is run is a separate thread. Beware:
		this approach is not recommended since R does not support
		real multithreading yet
   FORKED     - each connection is forked to a new process. This is the
                recommended way to use this server. The advantage is (beside
		the fact that this works ;)) that each client has a separate
		namespace since the processes are independent
   - if none of the above is specified then cooperative serving is used
     (which is currently the only way available in Windows - if embedding R
     worked in that setup)
*/

#define USE_RINTERNALS
#define SOCK_ERRORS
#define USE_SNPRINTF
#define LISTENQ 16

#include <stdio.h>
#include <sisocks.h>
#ifdef THREADED
#include <sbthread.h>
#endif
#ifdef FORKED
#include <sys/wait.h>
#endif
#include <R.h>
#include <Rinternals.h>
#include <IOStuff.h>
#include <Parse.h>
#include "Rsrv.h"

/* send buffer size (default 2MB) */
#define sndBS (2048*1024)

int port = default_Rsrv_port;
int active = 1;

char **top_argv;
int top_argc;

void jump_now()
{
  extern void Rf_resetStack(int topLevel);
  fprintf(stderr, "Handling R error locally\n");
  //Rf_resetStack(1);
  //elog(ERROR, "Error in R");
}

char *getParseName(int n) {
  switch(n) {
  case PARSE_NULL: return "null";
  case PARSE_OK: return "ok";
  case PARSE_INCOMPLETE: return "incomplete";
  case PARSE_ERROR: return "error";
  case PARSE_EOF: return "EOF";
  };
  return "<unknown>";
};

struct tenc {
  int ptr;
  int *id[256];
  int ty[256];
  int *buf;
};

#define attrFixup if (hasAttr) buf=storeSEXP(buf,ATTRIB(x));
#define dist(A,B) (((int)(((char*)B)-((char*)A)))-4)

int* storeSEXP(int* buf, SEXP x) {
  int t=TYPEOF(x);
  int i;
  char c;
  int hasAttr=0;
  int *preBuf=buf;

  if (TYPEOF(ATTRIB(x))>0) hasAttr=XT_HAS_ATTR;

  if (t==NILSXP) {
    *buf=XT_NULL|hasAttr;
    buf++;
    attrFixup;
    goto didit;
  } 
  
  if (t==LISTSXP) {
    *buf=XT_LIST|hasAttr;
    buf++;
    attrFixup;
    buf=storeSEXP(buf,CAR(x));
    buf=storeSEXP(buf,CDR(x));    
    goto didit;
  };

  if (t==LANGSXP) {
    *buf=XT_LANG|hasAttr;
    buf++;
    attrFixup;
    goto didit;
  };

  if (t==REALSXP) {
    if (LENGTH(x)>1) {
      *buf=XT_ARRAY_DOUBLE|hasAttr;
      buf++;
      attrFixup;
      while(i<LENGTH(x)) {
	((double*)buf)[i]=REAL(x)[i];
	i++;
      };
      buf=(int*)(((double*)buf)+LENGTH(x));
    } else {
      *buf=XT_DOUBLE|hasAttr;
      buf++;
      attrFixup;
      *((double*)buf)=*REAL(x);
      buf=(int*)(((double*)buf)+1);
    };
    goto didit;
  };

  if (t==EXPRSXP || t==VECSXP || t==STRSXP) {
    *buf=XT_VECTOR|hasAttr;
    buf++;
    attrFixup;
    i=0;
    while(i<LENGTH(x)) {
      buf=storeSEXP(buf,VECTOR_ELT(x,i));
      i++;
    };
    goto didit;
  };

  if (t==INTSXP) {
    *buf=XT_ARRAY_INT|hasAttr;
    buf++;
    attrFixup;
    i=0;
    while(i<LENGTH(x)) {
      *buf=INTEGER(x)[i];
      buf++;
      i++;
    };
    goto didit;
  };

  if (t==CHARSXP) {
    *buf=XT_STR|hasAttr;
    buf++;
    attrFixup;
    strcpy((char*)buf,(char*)STRING_PTR(x));
    buf=(int*)(((char*)buf)+strlen((char*)buf)+1);
    goto didit;
  };

  if (t==SYMSXP) {
    *buf=XT_SYM|hasAttr;
    buf++;
    attrFixup;
    buf=storeSEXP(buf,PRINTNAME(x));
    goto didit;
  };

  *buf=XT_UNKNOWN;
  buf++;
  *buf=TYPEOF(x);
  buf++;
  
 didit:
  *preBuf=SET_PAR(PAR_TYPE(*preBuf),dist(preBuf,buf));
  return buf;
};


void printSEXP(SEXP e) { 
  int t=TYPEOF(e);
  int i;
  char c;

  if (t==NILSXP) {
    printf("NULL value\n");
    return;
  };
  if (t==LANGSXP) {
    printf("language construct\n");
    return;
  };
  if (t==REALSXP) {
    if (LENGTH(e)>1) {
      printf("Vector of real variables: ");
      i=0;
      while(i<LENGTH(e)) {
	printf("%f",REAL(e)[i]);
	if (i<LENGTH(e)-1) printf(", ");
	i++;
      };
      putchar('\n');
    } else
      printf("Real variable %f\n",*REAL(e));
    return;
  };
  if (t==EXPRSXP) {
    printf("Vector of %d expressions:\n",LENGTH(e));
    i=0;
    while(i<LENGTH(e)) {
      printSEXP(VECTOR_ELT(e,i));
      i++;
    };
    return;
  };
  if (t==INTSXP) {
    printf("Vector of %d integers:\n",LENGTH(e));
    i=0;
    while(i<LENGTH(e)) {
      printf("%d",INTEGER(e)[i]);
      if (i<LENGTH(e)-1) printf(", ");
      i++;
    };
    putchar('\n');
    return;
  };
  if (t==VECSXP) {
    printf("Vector of %d fields:\n",LENGTH(e));
    i=0;
    while(i<LENGTH(e)) {
      printSEXP(VECTOR_ELT(e,i));
      i++;
    };
    return;
  };
  if (t==STRSXP) {
    i=0;
    printf("String vector of length %d:\n",LENGTH(e));
    while(i<LENGTH(e)) {
      printSEXP(VECTOR_ELT(e,i)); i++;
    };
    return;
  };
  if (t==CHARSXP) {
    printf("scalar string: \"%s\"\n",STRING_PTR(e));
    return;
  };
  if (t==SYMSXP) {
    printf("Symbol, name: "); printSEXP(PRINTNAME(e));
    return;
  };
  printf("Unknown type: %d\n",t);
};

void printBufInfo(IoBuffer *b) {
  printf("read-off: %d, write-off: %d\n",b->read_offset,b->write_offset);
};

int localonly=1;

SOCKET ss;

/* arguments structure passed to a working thread */
struct args {
  int s;
  SAIN sa;
};

void sendResp(int s, int rsp) {
  struct phdr ph;
  memset(&ph,0,sizeof(ph));
  ph.cmd=rsp|CMD_RESP;
  send(s,&ph,sizeof(ph),0);
};

void sendRespData(int s, int rsp, int len, void *buf) {
  struct phdr ph;
  memset(&ph,0,sizeof(ph));
  ph.cmd=rsp|CMD_RESP;
  ph.len=len;
  send(s,&ph,sizeof(ph),0);
  send(s,buf,len,0);
};

char *IDstring="Rsrv0100QAP1\r\n\r\n--------------\r\n";

#define inBuf 2048

#ifndef decl_sbthread
#define decl_sbthread void
#endif

decl_sbthread newConn(void *thp) {
  SOCKET s;
  struct args *a=(struct args*)thp;
  struct phdr ph;
  char buf[inBuf+8], *c;
  int *par[16];
  int pars;
  int i,j,k,n;
  int process;
  int stat;
  char *sendbuf;
  char *tail;
  
  IoBuffer *iob;
  SEXP xp,exp;
    
  memset(buf,0,inBuf+8);
#ifdef FORKED  
  if (fork()!=0) return;
#endif
  sendbuf=(char*)malloc(sndBS);
  printf("connection accepted.\n");
  s=a->s;
  free(a);
  send(s,IDstring,32,0);
  while((n=recv(s,&ph,sizeof(ph),0))==sizeof(ph)) {
    process=0;
    pars=0;
    if (ph.len>0) {
      if (ph.len<inBuf) {
	printf("loading buffer (awaiting %d bytes)\n",ph.len);
	i=0;
	while(n=recv(s,buf+i,ph.len-i,0)) {
	  if (n>0) i+=n;
	  if (i>=ph.len || n<1) break;
	};
	if (i<ph.len) break;
	memset(buf+ph.len,0,8);
	
	printf("parsing parameters\n");
	c=buf+ph.dof;
	while((c<buf+ph.len) && (i=*((int*)c))) {
	  par[pars]=(int*)c;
	  pars++;
	  c+=PAR_LEN(i);
	  if (pars>15) break;
	}; /* we don't parse more than 16 parameters */
	i=0;
	while(i<ph.len) printf("%02x ",buf[i++]);
	puts("");
      } else {
	printf("discarding buffer because too big (awaiting %d bytes)\n",ph.len);
	i=ph.len;
	while(n=recv(s,buf,i>inBuf?inBuf:i,0)) {
	  if (n>0) i-=n;
	  if (i<1 || n<1) break;
	};
	if (i>0) break;
	/* if the pars are bigger than my buffer, send inv_par response */
	sendResp(s,SET_STAT(RESP_ERR,ERR_inv_par));
	process=1; ph.cmd=0;
      };
    };

    printf("CMD=%08x, pars=%d\n",ph.cmd,pars);

    if (ph.cmd==CMD_shutdown) {
      sendResp(s,RESP_OK);
      printf("clean shutdown.\n");
      active=0;
      closesocket(s);
      free(sendbuf);
      return;
    };

    if (ph.cmd==CMD_voidEval || ph.cmd==CMD_eval) {
      process=1;
      if (pars<1 || PAR_TYPE(*par[0])!=DT_STRING) 
	sendResp(s,SET_STAT(RESP_ERR,ERR_inv_par));
      else {
	c=(char*)(par[0]+1);
	//printf("eval of \"%s\" requested.\n",c);
	i=j=0; /* count the lines to pass the right parameter to parse
		  the string should contain a trainig \n !! */
	while(c[i]) if(c[i++]=='\n') j++;
	printf("R_IoBufferPuts(\"%s\",iob)\n",c);
	/* R_IoBufferWriteReset(iob);
	   R_IoBufferReadReset(iob); */
	R_IoBufferInit(iob);
	R_IoBufferPuts(c,iob);
	printf("R_Parse1Buffer(iob,%d,&stat)\n",j);
	xp=R_Parse1Buffer(iob,j,&stat);
	printf("buffer parsed, stat=%d\nRf_eval(xp,R_GlobalEnv);\n",stat);
	exp=Rf_eval(xp,R_GlobalEnv);
	printf("buffer evaluated.\n");
	printSEXP(exp);
	if (ph.cmd==CMD_voidEval)
	  sendResp(s,RESP_OK);
	else {
	  tail=(char*)storeSEXP((int*)sendbuf,exp);
	  printf("stored SEXP; length=%d\n",tail-sendbuf);
	  sendRespData(s,RESP_OK,tail-sendbuf,sendbuf);
	};
	printf("\nOK sent\n");
      };
    };

    if (!process)
      sendResp(s,SET_STAT(RESP_ERR,ERR_inv_cmd));
  };    
  printf("malformed packet or connection terminated (n=%d). closing socket.\n",n);
  if (n>0)
    sendResp(s,SET_STAT(RESP_ERR,ERR_conn_broken));
  closesocket(s);
  free(sendbuf);
  printf("done.\n");
};

void serverLoop() {
  SAIN ssa;
  int al;
  int reuse;
  struct args *sa;
  struct sockaddr_in lsa;

  lsa.sin_addr.s_addr=inet_addr("127.0.0.1");

  initsocks();
  ss=FCF("open socket",socket(AF_INET,SOCK_STREAM,0));
  reuse=1; /* enable socket address reusage */
  setsockopt(ss,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
  FCF("bind",bind(ss,build_sin(&ssa,0,port),sizeof(ssa)));
  FCF("listen",listen(ss,LISTENQ));
  while(active) { /* main serving loop */
#ifdef FORKED
    while (waitpid(-1,0,WNOHANG)>0);
#endif
    sa=(struct args*)malloc(sizeof(struct args));
    sa->s=CF("accept",accept(ss,(SA*)&(sa->sa),&al));
    if (localonly) {
      if (sa->sa.sin_addr.s_addr==lsa.sin_addr.s_addr)
#ifdef THREADED
        sbthread_create(newConn,sa);
#else
	newConn(sa);
#endif
      else
        closesocket(sa->s);
    } else
#ifdef THREADED
      sbthread_create(newConn,sa); 
#else
      newConn(sa);
#endif
  };
};

int main(int argc, char **argv)
{
  IoBuffer *b;
  int stat;
  SEXP r,s;
  SEXP env;
  char c;

  top_argc=argc; top_argv=argv;

  printf("Rf_initEmbedR returned %d\n",Rf_initEmbeddedR(top_argc,top_argv));

  R_IoBufferInit(b);
  //printBufInfo(b);
  R_IoBufferPuts("data(iris)\n",b);
  r=R_Parse1Buffer(b,1,&stat);r=Rf_eval(r,R_GlobalEnv);
  
  serverLoop();
};
