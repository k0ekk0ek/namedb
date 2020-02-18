#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "dname.h"

static int
hexdigit_to_int(char chr)
{
  switch (chr) {
    case '0': return 0;
    case '1': return 1;
    case '2': return 2;
    case '3': return 3;
    case '4': return 4;
    case '5': return 5;
    case '6': return 6;
    case '7': return 7;
    case '8': return 8;
    case '9': return 9;
    case 'a': case 'A': return 10;
    case 'b': case 'B': return 11;
    case 'c': case 'C': return 12;
    case 'd': case 'D': return 13;
    case 'e': case 'E': return 14;
    case 'f': case 'F': return 15;
    default:
      break;
  }
  abort();
}

#define MAXDOMAINLEN 255
#define MAXLABELLEN 63

int dname_parse_wire(uint8_t* dname, const char* name)
{
       const uint8_t *s = (const uint8_t *) name;
       uint8_t *h;
       uint8_t *p;
       uint8_t *d = dname;
       size_t label_length;

       if (strcmp(name, ".") == 0) {
               /* Root domain.  */
               dname[0] = 0;
               return 1;
       }

       for (h = d, p = h + 1; *s; ++s, ++p) {
               if (p - dname >= MAXDOMAINLEN) {
                       return 0;
               }

               switch (*s) {
               case '.':
                       if (p == h + 1) {
                               /* Empty label.  */
                               return 0;
                       } else {
                               label_length = p - h - 1;
                               if (label_length > MAXLABELLEN) {
                                       return 0;
                               }
                               *h = label_length;
                               h = p;
                       }
                       break;
               case '\\':
                       /* Handle escaped characters (RFC1035 5.1) */
                       if (isdigit((unsigned char)s[1]) && isdigit((unsigned char)s[2]) && isdigit((unsigned char)s[3])) {
                               int val = (hexdigit_to_int(s[1]) * 100 +
                                          hexdigit_to_int(s[2]) * 10 +
                                          hexdigit_to_int(s[3]));
                               if (0 <= val && val <= 255) {
                                       s += 3;
                                       *p = val;
                               } else {
                                       *p = *++s;
                               }
                       } else if (s[1] != '\0') {
                               *p = *++s;
                       }
                       break;
               default:
                       *p = *s;
                       break;
               }
       }

       if (p != h + 1) {
               /* Terminate last label.  */
               label_length = p - h - 1;
               if (label_length > MAXLABELLEN) {
                       return 0;
               }
               *h = label_length;
               h = p;
       }

       /* Add root label.  */
       if (h - dname >= MAXDOMAINLEN) {
               return 0;
       }
       *h = 0;

       return p-dname;
}
