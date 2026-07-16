#ifndef __RTC_H
#define __RTC_H

extern uint16_t RTC_time[];
void rtc_Init(void);
void rtc_settime(void);

void rtc_readtime(void);

#endif
