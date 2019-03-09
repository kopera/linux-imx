#ifndef __TAS5825M_H__
#define __TAS5825M_H__

#include <linux/regmap.h>

static const struct reg_sequence tas5825m_init_sequence[] = 
{
	{ 0x00, 0x00 },
	{ 0x7f, 0x00 },
	{ 0x00, 0x00 },
	{ 0x03, 0x03 },
	{ 0x78, 0x80 },
};

#endif