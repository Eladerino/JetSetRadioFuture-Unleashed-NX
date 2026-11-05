/* Free-skate mission destinations from JuMatt's JSRF Unleashed 0.6.6 map
 * selector (commit df1af3d0c01fd93800876183f73976ad4aa92e29). Mission ids
 * are chapter << 16 | (100 + the mission number). Hikage Street's 0925 is a
 * flag battle and 0926 does not load, so its free-skate destination is 0825. */
#ifndef JSRF_MAPS_H
#define JSRF_MAPS_H

#include <stdint.h>

typedef struct { const char *name; const char *district; uint32_t mission; } JsrfMap;

static const JsrfMap k_jsrf_maps[] = {
    { "Shibuya Terminal",                    "Shibuya-cho", 0x0009006Eu },
    { "Chuo Street",                         "Shibuya-cho", 0x00090073u },
    { "Dogenzaka Hill",                      "Shibuya-cho", 0x00090078u },
    { "Hikage Street",                       "Shibuya-cho", 0x0008007Du },
    { "Rokkaku-dai Heights",                 "Kogane-cho",  0x00090082u },
    { "Tokyo Underground Sewage Facility",   "Kogane-cho",  0x00090087u },
    { "Kibogaoka Hill",                       "Kogane-cho",  0x0009008Cu },
    { "Bottom Point of the Sewage Facility", "Kogane-cho",  0x00090091u },
    { "Fortified Residential Zone",           "Kogane-cho",  0x00090096u },
    { "99th Street",                          "Benten-cho",  0x0009009Bu },
    { "Sky Dinosaurian Square",               "Benten-cho",  0x000900A0u },
    { "Rokkaku Expo Stadium",                 "Benten-cho",  0x000900A5u },
    { "Highway Zero",                         "Benten-cho",  0x000900AAu },
    { "Skyscraper District",                  "Benten-cho",  0x000900AFu },
};
#define JSRF_MAP_COUNT ((int)(sizeof k_jsrf_maps / sizeof k_jsrf_maps[0]))

#endif
