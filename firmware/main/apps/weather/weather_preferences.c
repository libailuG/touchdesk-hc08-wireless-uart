#include "weather.h"
uint32_t weather_config_encode(unsigned city,unsigned theme,bool dynamic)
{
    return 0x20000u|(dynamic?0x400u:0)|(theme<<8)|city;
}
bool weather_config_decode(uint32_t config,unsigned *city,unsigned *theme,bool *dynamic)
{
    if((config&~0x7ffu)!=0x20000u||(config&255)>=WEATHER_CITIES||((config>>8)&3)>=WEATHER_THEMES) return false;
    *city=config&255; *theme=(config>>8)&3; *dynamic=(config&0x400)!=0;
    return true;
}
