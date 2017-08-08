/* dump_dns.c - library function to emit decoded dns message on a FILE.
 *
 * By: Paul Vixie, ISC, October 2007
 */

/*
 * Copyright (c) 2016-2017, OARC, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "dnscap_common.h"
#include "dump_dns.h"

#include <arpa/nameser.h>
#include <arpa/inet.h>
#include <errno.h>
#include <resolv.h>
#include <string.h>
#include <stdlib.h>

extern const char *p_rcode(int rcode);

static const char *p_opcode(int opcode);

static void dump_dns_sect(ns_msg *, ns_sect, FILE *, const char *);

static void dump_dns_rr(ns_msg *, ns_rr *, ns_sect, FILE *);

#define MY_GET16(s, cp) do { \
  register const u_char *t_cp = (const u_char *)(cp); \
  (s) = ((u_int16_t)t_cp[0] << 8) \
      | ((u_int16_t)t_cp[1]) \
      ; \
  (cp) += NS_INT16SZ; \
} while (0)

#define MY_GET32(l, cp) do { \
  register const u_char *t_cp = (const u_char *)(cp); \
  (l) = ((u_int32_t)t_cp[0] << 24) \
      | ((u_int32_t)t_cp[1] << 16) \
      | ((u_int32_t)t_cp[2] << 8) \
      | ((u_int32_t)t_cp[3]) \
      ; \
  (cp) += NS_INT32SZ; \
} while (0)

const char *
hostname_from_question(ns_msg msg) {
  static char hostname[256] = {0};
  ns_rr rr;
  int rrnum, rrmax;
  const char *result;
  int result_len;
  rrmax = ns_msg_count(msg, ns_s_qd);
  if (rrmax == 0)
    return NULL;
  for (rrnum = 0; rrnum < rrmax; rrnum++) {
    if (ns_parserr(&msg, ns_s_qd, rrnum, &rr)) {
      printf("ns_parserr\n");
      return NULL;
    }
    result = ns_rr_name(rr);
    result_len = strlen(result) + 1;
    if (result_len > sizeof(hostname)) {
      printf("hostname too long: %s\n", result);
    }
    memset(hostname, 0, sizeof(hostname));
    memcpy(hostname, result, result_len);
    return hostname;
  }
  return NULL;
}


char *get_query_domain(const u_char *payload, size_t paylen, FILE *trace) {
  ns_msg msg;

  // Message too long
  if (ns_initparse(payload, paylen, &msg) < 0) {
    fputs(strerror(errno), trace);
    printf("\n");
    return NULL;
  }

  const char *host = hostname_from_question(msg);
  return host;
}


void
dump_dns(const u_char *payload, size_t paylen,
         FILE *trace, const char *endline) {
  u_int opcode, rcode, id;
  const char *sep;
  ns_msg msg;

  fprintf(trace, " %sdns ", endline);
  if (ns_initparse(payload, paylen, &msg) < 0) {
    fputs(strerror(errno), trace);
    return;
  }
  opcode = ns_msg_getflag(msg, ns_f_opcode);
  rcode = ns_msg_getflag(msg, ns_f_rcode);
  id = ns_msg_id(msg);
  fprintf(trace, "%s,%s,%u", p_opcode(opcode), p_rcode(rcode), id);
  sep = ",";
#define FLAG(t, f) if (ns_msg_getflag(msg, f)) { \
      fprintf(trace, "%s%s", sep, t); \
      sep = "|"; \
      }
  FLAG("qr", ns_f_qr);
  FLAG("aa", ns_f_aa);
  FLAG("tc", ns_f_tc);
  FLAG("rd", ns_f_rd);
  FLAG("ra", ns_f_ra);
  FLAG("z", ns_f_z);
  FLAG("ad", ns_f_ad);
  FLAG("cd", ns_f_cd);
#undef FLAG
  dump_dns_sect(&msg, ns_s_qd, trace, endline);
  dump_dns_sect(&msg, ns_s_an, trace, endline);
  dump_dns_sect(&msg, ns_s_ns, trace, endline);
  dump_dns_sect(&msg, ns_s_ar, trace, endline);
}

static void
dump_dns_sect(ns_msg *msg, ns_sect sect, FILE *trace, const char *endline) {
  int rrnum, rrmax;
  const char *sep;
  ns_rr rr;

  rrmax = ns_msg_count(*msg, sect);
  if (rrmax == 0) {
    fputs(" 0", trace);
    return;
  }
  fprintf(trace, " %s%d", endline, rrmax);
  sep = "";
  for (rrnum = 0; rrnum < rrmax; rrnum++) {
    if (ns_parserr(msg, sect, rrnum, &rr)) {
      fputs(strerror(errno), trace);
      return;
    }
    fprintf(trace, " %s", sep);
    dump_dns_rr(msg, &rr, sect, trace);
    sep = endline;
  }
}

static void
dump_dns_rr(ns_msg *msg, ns_rr *rr, ns_sect sect, FILE *trace) {
  char buf[NS_MAXDNAME];
  u_int class, type;
  const u_char *rd;
  u_int32_t soa[5];
  u_int16_t mx;
  int n;

  memset(buf, 0, sizeof(buf));
  class = ns_rr_class(*rr);
  type = ns_rr_type(*rr);
  fprintf(trace, "%s,%s,%s",
          ns_rr_name(*rr),
          p_class(class),
          p_type(type));
  if (sect == ns_s_qd)
    return;
  fprintf(trace, ",%lu", (u_long) ns_rr_ttl(*rr));
  rd = ns_rr_rdata(*rr);
  switch (type) {
    case ns_t_soa:
      n = ns_name_uncompress(ns_msg_base(*msg), ns_msg_end(*msg),
                             rd, buf, sizeof buf);
          if (n < 0)
            goto error;
          putc(',', trace);
          fputs(buf, trace);
          rd += n;
          n = ns_name_uncompress(ns_msg_base(*msg), ns_msg_end(*msg),
                                 rd, buf, sizeof buf);
          if (n < 0)
            goto error;
          putc(',', trace);
          fputs(buf, trace);
          rd += n;
          if (ns_msg_end(*msg) - rd < 5 * NS_INT32SZ)
            goto error;
          for (n = 0; n < 5; n++)
            MY_GET32(soa[n], rd);
          sprintf(buf, "%u,%u,%u,%u,%u",
                  soa[0], soa[1], soa[2], soa[3], soa[4]);
          break;
    case ns_t_a:
      if (ns_msg_end(*msg) - rd < 4)
        goto error;
          inet_ntop(AF_INET, rd, buf, sizeof buf);
          break;
    case ns_t_aaaa:
      if (ns_msg_end(*msg) - rd < 16)
        goto error;
          inet_ntop(AF_INET6, rd, buf, sizeof buf);
          break;
    case ns_t_mx:
      if (ns_msg_end(*msg) - rd < 2)
        goto error;
          MY_GET16(mx, rd);
          fprintf(trace, ",%u", mx);
          /* FALLTHROUGH */
    case ns_t_ns:
    case ns_t_ptr:
    case ns_t_cname:
      n = ns_name_uncompress(ns_msg_base(*msg), ns_msg_end(*msg),
                             rd, buf, sizeof buf);
          if (n < 0)
            goto error;
          break;
          /*
             * GGM 2014/09/04 deal with edns0 a bit more clearly
             */
    case ns_t_opt: {
      u_long edns0csize;
      u_short edns0version;
      u_short edns0rcode;
      u_char edns0dobit;
      u_char edns0z;

      /* class encodes client UDP size accepted */
      edns0csize = (u_long) (class);

      /*
             * the first two bytes of ttl encode edns0 version, and the extended rcode
             */
      edns0version = ((u_long) ns_rr_ttl(*rr) & 0x00ff0000) >> 16;
      edns0rcode = ((u_long) ns_rr_ttl(*rr) & 0xff000000) >> 24;

      /*
             *  the next two bytes of ttl encode DO bit as the top bit, and the remainder is the 'z' value
             */
      edns0dobit = (u_long) ns_rr_ttl(*rr) & 0x8000 ? '1' : '0';
      edns0z = (u_long) ns_rr_ttl(*rr) & 0x7fff;

      /* optlen is the size of the OPT rdata */
      u_short optlen = ns_rr_rdlen(*rr);

      fprintf(trace, ",edns0[len=%d,UDP=%lu,ver=%d,rcode=%d,DO=%c,z=%d] %c\n\t",
              optlen, edns0csize, edns0version, edns0rcode, edns0dobit, edns0z, '\\');

      /* if we have any data */
      while (optlen >= 4) {
        /* the next two shorts are the edns0 opt code, and the length of the optionsection */
        u_short edns0optcod;
        u_short edns0lenopt;
        MY_GET16(edns0optcod, rd);
        MY_GET16(edns0lenopt, rd);
        optlen -= 4;
        fprintf(trace, "edns0[code=%d,codelen=%d] ", edns0optcod, edns0lenopt);

        /*
                 * Check that the OPTION-LENGTH for this EDNS0 option doesn't
                 * exceed the size of the remaining OPT record rdata.  If it does,
                 * just bail.
                 */
        if (edns0lenopt > optlen)
          goto error;

        /*
                 * "pre-consume" edns0lenopt bytes from optlen here because
                 * below we're going to decrement edns0lenopt as we go.
                 * At this point optlen will refer to the size of the remaining
                     * OPT_T rdata after parsing the current option.
                 */
        optlen -= edns0lenopt;

        /* if we have edns0_client_subnet */
        if (edns0optcod == 0x08) {
          if (edns0lenopt < 4)
            goto error;
          u_short afi;
          MY_GET16(afi, rd);
          u_short masks;
          MY_GET16(masks, rd);
          edns0lenopt -= 4;
          u_short srcmask = (masks & 0xff00) >> 8;
          u_short scomask = (masks & 0xff);

          char buf[128];
          u_char addr[16];
          memset(addr, 0, sizeof addr);
          memcpy(addr, rd, edns0lenopt < sizeof(addr) ? edns0lenopt : sizeof(addr));

          if (afi == 0x1) {
            inet_ntop(AF_INET, addr, buf, sizeof buf);
          } else if (afi == 0x2) {
            inet_ntop(AF_INET6, addr, buf, sizeof buf);
          } else {
            fprintf(trace, "unknown AFI %d\n", afi);
            strcpy(buf, "<unknown>");
          }
          fprintf(trace, "edns0_client_subnet=%s/%d (scope %d)", buf, srcmask, scomask);
        }
        /* increment the rd pointer by the remaining option data size */
        rd += edns0lenopt;
      }
    }
          break;

    default:
    error:
      sprintf(buf, "[%u]", ns_rr_rdlen(*rr));
  }
  if (buf[0] != '\0') {
    putc(',', trace);
    fputs(buf, trace);
  }
}

static const char *
p_opcode(int opcode) {
  static char buf[20];
  switch (opcode) {
    case 0:
      return "QUERY";
          break;
    case 1:
      return "IQUERY";
          break;
    case 2:
      return "CQUERYM";
          break;
    case 3:
      return "CQUERYU";
          break;
    case 4:
      return "NOTIFY";
          break;
    case 5:
      return "UPDATE";
          break;
    case 14:
      return "ZONEINIT";
          break;
    case 15:
      return "ZONEREF";
          break;
    default:
      snprintf(buf, sizeof(buf), "OPCODE%d", opcode);
          return buf;
          break;
  }
  /* NOTREACHED */
}
