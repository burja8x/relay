#!/usr/bin/env python3

# python3 -m pip install urllib unidecode

import urllib.request
import unidecode

ATR_URL='https://raw.githubusercontent.com/LudovicRousseau/pcsc-tools/master/smartcard_list.txt'

C_HEADER="""//-----------------------------------------------------------------------------
// List scraped from
// """ + ATR_URL + """
// Copyright (C) 2002-2021  Ludovic Rousseau
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
#ifndef ATRS_H__

#define ATRS_H__
#include <stddef.h>

typedef struct atr_s {
    const char *bytes;
    const char *desc;
} atr_t;

const char *getAtrInfo(const char *atr_str);

// atr_t array is expected to be NULL terminated
const static atr_t AtrTable[] = {
    { "3BDF18FFC080B1FE751F033078464646462026204963656D616E1D", "Cardhelper by 0xFFFF and Iceman" },
"""

C_FOOTER="""    {NULL, "N/A"}
};

#endif
"""

def main():
    with open('src/atrs.h','w') as fatr:
        s = urllib.request.urlopen(ATR_URL).read().decode()
        atr = None
        desc = ''
        fatr.write(C_HEADER)
        for line in s.split('\n'):
            if len(line) == 0 or line[0] == '#':
                continue
            if line[0] == '\t':
                desc += ['\\n',''][len(desc)==0] + unidecode.unidecode(line[1:]).replace('"',"'").replace('\\','\\\\')
            else:
                if atr is not None:
                    fatr.write(f'    {{ "{atr}", "{desc}" }},\n')
                atr = line.replace(' ','')
                desc = ''
        fatr.write(f'    {{ "{atr}", "{desc}" }},\n')
        fatr.write(C_FOOTER)

if __name__ == "__main__":
    main()
