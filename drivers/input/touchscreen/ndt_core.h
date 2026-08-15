#ifndef __NDT_CORE_H__
#define __NDT_CORE_H__

extern int ndt_get_pressure_f60(int touch_flag, int x, int y);
extern int ndt_get_pressure_m65(int touch_flag, int x, int y);
static inline int ndt_get_pressure(int touch_flag, int x, int y)
{
	int pressure = 1;
#ifdef CONFIG_INPUT_PRESS_NDT_F60
	pressure = ndt_get_pressure_f60(touch_flag, x , y);
#endif
#ifdef CONFIG_INPUT_PRESS_NDT_M65
	if (pressure == 1)
		pressure = ndt_get_pressure_m65(touch_flag, x , y);
#endif
	return pressure;
}

#endif /* __NDT_CORE_H__ */
