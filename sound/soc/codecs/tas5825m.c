// SPDX-License-Identifier: GPL-2.0
// Audio driver for TAS5825M Audio Amplifier
// Copyright (C) 2019 KOPERA
// Ali Sabil <ali.sabil@koperadev.com>


#include <linux/types.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <sound/pcm.h>
#include <sound/initval.h>

#include "tas5825m.h"
#include "tas5825m-init.h"


#define TAS5825M_NUM_SUPPLIES 2
static const char * const tas5825m_supply_names[TAS5825M_NUM_SUPPLIES] = {
	"dvdd",    /* Digital power supply. Connect to 3.3-V supply. */
	"pvdd",    /* Class-D amp and analog power supply (connected). */
};

struct tas5825m_priv {
	struct regulator_bulk_data supplies[TAS5825M_NUM_SUPPLIES];

	struct regmap             *regmap_physical;
	struct regmap             *regmap;

	uint8_t                    book_id;
	uint8_t                    page_id;

	struct mutex               volume_lock;
	struct mutex               eq_biquad_lock;
};

/*
    Regmap: physical
*/

static const struct regmap_config tas5825m_regmap_physical_config = {
	.name = "physical",
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x7f,
	.cache_type = REGCACHE_NONE,
};

/*
    Regmap: virtual
*/

static bool tas5825m_reg_accessible(unsigned int reg)
{
	uint8_t book_id = TAS5825M_REG_BOOK_ID(reg);
	uint8_t page_id = TAS5825M_REG_PAGE_ID(reg);
	uint8_t reg_id = TAS5825M_REG_REG_ID(reg);

	return (reg_id >= 0x01 && reg_id <= 0x7f) &&
	           ((book_id == 0x00 && page_id == 0x00) ||
	           (book_id == 0x78 && page_id == 0x01 &&
	               ((reg_id >= 0x48 && reg_id < 0x48 + 4) || (reg_id >= 0x7c && reg_id < 0x7c + 4))) ||
	           (book_id == 0x8c && page_id == 0x01) ||
	           (book_id == 0x8c && page_id == 0x06) ||
	           (book_id == 0x8c && page_id == 0x07) ||
	           (book_id == 0x8c && page_id == 0x09) ||
	           (book_id == 0x8c && page_id == 0x0a) ||
	           (book_id == 0x8c && page_id == 0x0b) ||
	           (book_id == 0x8c && page_id == 0x0c) ||
	           (book_id == 0xaa && (page_id >= 0x01 && page_id <= 0x0a)));
}

static bool tas5825m_reg_volatile(struct device *dev, unsigned int reg)
{
	return (TAS5825M_REG_IS_PAGE_SELECT(reg)) ||
	       (TAS5825M_REG_IS_BOOK_SELECT(reg)) ||
	       !tas5825m_reg_accessible(reg);
}

static bool tas5825m_reg_readable(struct device *dev, unsigned int reg)
{
	return tas5825m_reg_accessible(reg);
}

static bool tas5825m_reg_writeable(struct device *dev, unsigned int reg)
{
	return !(TAS5825M_REG_IS_PAGE_SELECT(reg)) &&
	       !(TAS5825M_REG_IS_BOOK_SELECT(reg)) &&
	       tas5825m_reg_accessible(reg);
}

static int tas5825m_reg_sync_book_page(struct tas5825m_priv *priv, uint8_t book_id, uint8_t page_id)
{
	int ret = 0;

	if (book_id != priv->book_id) {
		if (priv->page_id != 0) {
			ret = regmap_write(priv->regmap_physical, 0x00, 0x00);
			if (ret != 0)
				goto out;
		}
		ret = regmap_write(priv->regmap_physical, 0x7f, book_id);
		if (ret != 0)
			goto out;
		ret = regmap_write(priv->regmap_physical, 0x00, page_id);
		if (ret != 0)
			goto out;

		priv->book_id = book_id;
		priv->page_id = page_id;
	} else if (page_id != priv->page_id) {
		ret = regmap_write(priv->regmap_physical, 0x00, page_id);
		if (ret != 0)
			goto out;
		priv->page_id = page_id;
	}

out:
	return ret;
}

static int tas5825m_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	struct tas5825m_priv *priv = context;
	uint8_t book_id = TAS5825M_REG_BOOK_ID(reg);
	uint8_t page_id = TAS5825M_REG_PAGE_ID(reg);
	uint8_t reg_id = TAS5825M_REG_REG_ID(reg);
	int ret;
	
	ret = tas5825m_reg_sync_book_page(priv, book_id, page_id);
	if (ret != 0)
		return ret;

	ret = regmap_read(priv->regmap_physical, reg_id, val);
	if (ret != 0)
		return ret;

	*val &= 0xff;
	return 0;
}

static int tas5825m_reg_write(void *context, unsigned int reg, unsigned int val)
{
	struct tas5825m_priv *priv = context;
	uint8_t book_id = TAS5825M_REG_BOOK_ID(reg);
	uint8_t page_id = TAS5825M_REG_PAGE_ID(reg);
	uint8_t reg_id = TAS5825M_REG_REG_ID(reg);
	int ret;
	
	ret = tas5825m_reg_sync_book_page(priv, book_id, page_id);
	if (ret != 0)
		return ret;

	return regmap_write(priv->regmap_physical, reg_id, val & 0xff);
}

static uint32_t tas5825m_reg_next(uint32_t reg)
{
	uint8_t book_id = TAS5825M_REG_BOOK_ID(reg);
	uint8_t page_id = TAS5825M_REG_PAGE_ID(reg);
	uint8_t reg_id = TAS5825M_REG_REG_ID(reg);

	if (book_id == 0x00) {
		if (reg_id < 0x7f) {
			return TAS5825M_REG(book_id, page_id, reg_id + 1);
		} else {
			return reg;
		}
	} else {
		if (reg_id < 0x7c) {
			return TAS5825M_REG(book_id, page_id, reg_id + 4);
		} else {
			return TAS5825M_REG(book_id, page_id + 1, 0x08);
		}
	}
}

static const struct regmap_config tas5825m_regmap_config = {
	.name = "virtual",
	.reg_bits = 24,
	.val_bits = 8,
	.max_register = TAS5825M_REG(0xaa, 0x06, 0x7f),

	.volatile_reg = tas5825m_reg_volatile,
	.readable_reg = tas5825m_reg_readable,
	.writeable_reg = tas5825m_reg_writeable,
	.reg_read = tas5825m_reg_read,
	.reg_write = tas5825m_reg_write,

	.cache_type = REGCACHE_RBTREE,
	.use_single_rw = true,
};


/*
    Controls
*/

static const uint32_t tas5825m_volume[256] = {
	0x00000038, /*   0: -103.5db */
	0x0000003b, /*   1: -103.0db */
	0x0000003f, /*   2: -102.5db */
	0x00000043, /*   3: -102.0db */
	0x00000047, /*   4: -101.5db */
	0x0000004b, /*   5: -101.0db */
	0x0000004f, /*   6: -100.5db */
	0x00000054, /*   7: -100.0db */
	0x00000059, /*   8:  -99.5db */
	0x0000005e, /*   9:  -99.0db */
	0x00000064, /*  10:  -98.5db */
	0x0000006a, /*  11:  -98.0db */
	0x00000070, /*  12:  -97.5db */
	0x00000076, /*  13:  -97.0db */
	0x0000007e, /*  14:  -96.5db */
	0x00000085, /*  15:  -96.0db */
	0x0000008d, /*  16:  -95.5db */
	0x00000095, /*  17:  -95.0db */
	0x0000009e, /*  18:  -94.5db */
	0x000000a7, /*  19:  -94.0db */
	0x000000b1, /*  20:  -93.5db */
	0x000000bc, /*  21:  -93.0db */
	0x000000c7, /*  22:  -92.5db */
	0x000000d3, /*  23:  -92.0db */
	0x000000df, /*  24:  -91.5db */
	0x000000ec, /*  25:  -91.0db */
	0x000000fa, /*  26:  -90.5db */
	0x00000109, /*  27:  -90.0db */
	0x00000119, /*  28:  -89.5db */
	0x0000012a, /*  29:  -89.0db */
	0x0000013b, /*  30:  -88.5db */
	0x0000014e, /*  31:  -88.0db */
	0x00000162, /*  32:  -87.5db */
	0x00000177, /*  33:  -87.0db */
	0x0000018d, /*  34:  -86.5db */
	0x000001a4, /*  35:  -86.0db */
	0x000001bd, /*  36:  -85.5db */
	0x000001d8, /*  37:  -85.0db */
	0x000001f4, /*  38:  -84.5db */
	0x00000211, /*  39:  -84.0db */
	0x00000231, /*  40:  -83.5db */
	0x00000252, /*  41:  -83.0db */
	0x00000275, /*  42:  -82.5db */
	0x0000029a, /*  43:  -82.0db */
	0x000002c2, /*  44:  -81.5db */
	0x000002ec, /*  45:  -81.0db */
	0x00000318, /*  46:  -80.5db */
	0x00000347, /*  47:  -80.0db */
	0x00000379, /*  48:  -79.5db */
	0x000003ad, /*  49:  -79.0db */
	0x000003e5, /*  50:  -78.5db */
	0x00000420, /*  51:  -78.0db */
	0x0000045f, /*  52:  -77.5db */
	0x000004a1, /*  53:  -77.0db */
	0x000004e7, /*  54:  -76.5db */
	0x00000532, /*  55:  -76.0db */
	0x00000580, /*  56:  -75.5db */
	0x000005d4, /*  57:  -75.0db */
	0x0000062c, /*  58:  -74.5db */
	0x0000068a, /*  59:  -74.0db */
	0x000006ed, /*  60:  -73.5db */
	0x00000756, /*  61:  -73.0db */
	0x000007c5, /*  62:  -72.5db */
	0x0000083b, /*  63:  -72.0db */
	0x000008b8, /*  64:  -71.5db */
	0x0000093c, /*  65:  -71.0db */
	0x000009c8, /*  66:  -70.5db */
	0x00000a5d, /*  67:  -70.0db */
	0x00000afa, /*  68:  -69.5db */
	0x00000ba0, /*  69:  -69.0db */
	0x00000c51, /*  70:  -68.5db */
	0x00000d0c, /*  71:  -68.0db */
	0x00000dd1, /*  72:  -67.5db */
	0x00000ea3, /*  73:  -67.0db */
	0x00000f81, /*  74:  -66.5db */
	0x0000106c, /*  75:  -66.0db */
	0x00001165, /*  76:  -65.5db */
	0x0000126d, /*  77:  -65.0db */
	0x00001385, /*  78:  -64.5db */
	0x000014ad, /*  79:  -64.0db */
	0x000015e6, /*  80:  -63.5db */
	0x00001733, /*  81:  -63.0db */
	0x00001893, /*  82:  -62.5db */
	0x00001a07, /*  83:  -62.0db */
	0x00001b92, /*  84:  -61.5db */
	0x00001d34, /*  85:  -61.0db */
	0x00001eef, /*  86:  -60.5db */
	0x000020c5, /*  87:  -60.0db */
	0x000022b6, /*  88:  -59.5db */
	0x000024c4, /*  89:  -59.0db */
	0x000026f2, /*  90:  -58.5db */
	0x00002941, /*  91:  -58.0db */
	0x00002bb2, /*  92:  -57.5db */
	0x00002e49, /*  93:  -57.0db */
	0x00003107, /*  94:  -56.5db */
	0x000033ef, /*  95:  -56.0db */
	0x00003703, /*  96:  -55.5db */
	0x00003a45, /*  97:  -55.0db */
	0x00003db9, /*  98:  -54.5db */
	0x00004161, /*  99:  -54.0db */
	0x00004541, /* 100:  -53.5db */
	0x0000495c, /* 101:  -53.0db */
	0x00004db5, /* 102:  -52.5db */
	0x0000524f, /* 103:  -52.0db */
	0x00005730, /* 104:  -51.5db */
	0x00005c5a, /* 105:  -51.0db */
	0x000061d3, /* 106:  -50.5db */
	0x0000679f, /* 107:  -50.0db */
	0x00006dc3, /* 108:  -49.5db */
	0x00007444, /* 109:  -49.0db */
	0x00007b28, /* 110:  -48.5db */
	0x00008274, /* 111:  -48.0db */
	0x00008a2e, /* 112:  -47.5db */
	0x0000925f, /* 113:  -47.0db */
	0x00009b0b, /* 114:  -46.5db */
	0x0000a43b, /* 115:  -46.0db */
	0x0000adf6, /* 116:  -45.5db */
	0x0000b845, /* 117:  -45.0db */
	0x0000c330, /* 118:  -44.5db */
	0x0000cec1, /* 119:  -44.0db */
	0x0000db01, /* 120:  -43.5db */
	0x0000e7fb, /* 121:  -43.0db */
	0x0000f5ba, /* 122:  -42.5db */
	0x00010449, /* 123:  -42.0db */
	0x000113b5, /* 124:  -41.5db */
	0x0001240c, /* 125:  -41.0db */
	0x0001355a, /* 126:  -40.5db */
	0x000147ae, /* 127:  -40.0db */
	0x00015b19, /* 128:  -39.5db */
	0x00016faa, /* 129:  -39.0db */
	0x00018573, /* 130:  -38.5db */
	0x00019c86, /* 131:  -38.0db */
	0x0001b4f8, /* 132:  -37.5db */
	0x0001cedc, /* 133:  -37.0db */
	0x0001ea49, /* 134:  -36.5db */
	0x00020756, /* 135:  -36.0db */
	0x0002261c, /* 136:  -35.5db */
	0x000246b5, /* 137:  -35.0db */
	0x0002693c, /* 138:  -34.5db */
	0x00028dcf, /* 139:  -34.0db */
	0x0002b48c, /* 140:  -33.5db */
	0x0002dd96, /* 141:  -33.0db */
	0x0003090d, /* 142:  -32.5db */
	0x00033718, /* 143:  -32.0db */
	0x000367de, /* 144:  -31.5db */
	0x00039b87, /* 145:  -31.0db */
	0x0003d240, /* 146:  -30.5db */
	0x00040c37, /* 147:  -30.0db */
	0x0004499d, /* 148:  -29.5db */
	0x00048aa7, /* 149:  -29.0db */
	0x0004cf8b, /* 150:  -28.5db */
	0x00051884, /* 151:  -28.0db */
	0x000565d1, /* 152:  -27.5db */
	0x0005b7b1, /* 153:  -27.0db */
	0x00060e6c, /* 154:  -26.5db */
	0x00066a4a, /* 155:  -26.0db */
	0x0006cb9a, /* 156:  -25.5db */
	0x000732ae, /* 157:  -25.0db */
	0x00079fde, /* 158:  -24.5db */
	0x00081385, /* 159:  -24.0db */
	0x00088e08, /* 160:  -23.5db */
	0x00090fcc, /* 161:  -23.0db */
	0x00099941, /* 162:  -22.5db */
	0x000a2adb, /* 163:  -22.0db */
	0x000ac515, /* 164:  -21.5db */
	0x000b6873, /* 165:  -21.0db */
	0x000c1580, /* 166:  -20.5db */
	0x000ccccd, /* 167:  -20.0db */
	0x000d8ef6, /* 168:  -19.5db */
	0x000e5ca1, /* 169:  -19.0db */
	0x000f367c, /* 170:  -18.5db */
	0x00101d3f, /* 171:  -18.0db */
	0x001111af, /* 172:  -17.5db */
	0x0012149a, /* 173:  -17.0db */
	0x001326dd, /* 174:  -16.5db */
	0x00144961, /* 175:  -16.0db */
	0x00157d1b, /* 176:  -15.5db */
	0x0016c311, /* 177:  -15.0db */
	0x00181c57, /* 178:  -14.5db */
	0x00198a13, /* 179:  -14.0db */
	0x001b0d7b, /* 180:  -13.5db */
	0x001ca7d7, /* 181:  -13.0db */
	0x001e5a84, /* 182:  -12.5db */
	0x002026f3, /* 183:  -12.0db */
	0x00220eaa, /* 184:  -11.5db */
	0x00241347, /* 185:  -11.0db */
	0x00263680, /* 186:  -10.5db */
	0x00287a27, /* 187:  -10.0db */
	0x002ae026, /* 188:   -9.5db */
	0x002d6a86, /* 189:   -9.0db */
	0x00301b71, /* 190:   -8.5db */
	0x0032f52d, /* 191:   -8.0db */
	0x0035fa27, /* 192:   -7.5db */
	0x00392cee, /* 193:   -7.0db */
	0x003c9038, /* 194:   -6.5db */
	0x004026e7, /* 195:   -6.0db */
	0x0043f405, /* 196:   -5.5db */
	0x0047facd, /* 197:   -5.0db */
	0x004c3ea8, /* 198:   -4.5db */
	0x0050c336, /* 199:   -4.0db */
	0x00558c4b, /* 200:   -3.5db */
	0x005a9df8, /* 201:   -3.0db */
	0x005ffc89, /* 202:   -2.5db */
	0x0065ac8c, /* 203:   -2.0db */
	0x006bb2d6, /* 204:   -1.5db */
	0x00721483, /* 205:   -1.0db */
	0x0078d6fd, /* 206:   -0.5db */
	0x00800000, /* 207:    0.0db */
	0x008795a0, /* 208:    0.5db */
	0x008f9e4d, /* 209:    1.0db */
	0x009820d7, /* 210:    1.5db */
	0x00a12478, /* 211:    2.0db */
	0x00aab0d5, /* 212:    2.5db */
	0x00b4ce08, /* 213:    3.0db */
	0x00bf84a6, /* 214:    3.5db */
	0x00caddc8, /* 215:    4.0db */
	0x00d6e30d, /* 216:    4.5db */
	0x00e39ea9, /* 217:    5.0db */
	0x00f11b6a, /* 218:    5.5db */
	0x00ff64c1, /* 219:    6.0db */
	0x010e86cf, /* 220:    6.5db */
	0x011e8e6a, /* 221:    7.0db */
	0x012f892c, /* 222:    7.5db */
	0x0141857f, /* 223:    8.0db */
	0x015492a4, /* 224:    8.5db */
	0x0168c0c6, /* 225:    9.0db */
	0x017e2105, /* 226:    9.5db */
	0x0194c584, /* 227:   10.0db */
	0x01acc17a, /* 228:   10.5db */
	0x01c62940, /* 229:   11.0db */
	0x01e11267, /* 230:   11.5db */
	0x01fd93c2, /* 231:   12.0db */
	0x021bc583, /* 232:   12.5db */
	0x023bc148, /* 233:   13.0db */
	0x025da234, /* 234:   13.5db */
	0x02818508, /* 235:   14.0db */
	0x02a78837, /* 236:   14.5db */
	0x02cfcc01, /* 237:   15.0db */
	0x02fa7292, /* 238:   15.5db */
	0x0327a01a, /* 239:   16.0db */
	0x03577aef, /* 240:   16.5db */
	0x038a2bad, /* 241:   17.0db */
	0x03bfdd56, /* 242:   17.5db */
	0x03f8bd7a, /* 243:   18.0db */
	0x0434fc5c, /* 244:   18.5db */
	0x0474cd1b, /* 245:   19.0db */
	0x04b865de, /* 246:   19.5db */
	0x05000000, /* 247:   20.0db */
	0x054bd843, /* 248:   20.5db */
	0x059c2f02, /* 249:   21.0db */
	0x05f14869, /* 250:   21.5db */
	0x064b6cae, /* 251:   22.0db */
	0x06aae84e, /* 252:   22.5db */
	0x07100c4d, /* 253:   23.0db */
	0x077b2e80, /* 254:   23.5db */
	0x07eca9cd, /* 255:   24.0db */
};

static uint8_t tas5825m_volume_to_index(uint32_t volume)
{
	size_t index;

	for (index = 0; index < ARRAY_SIZE(tas5825m_volume); index++) {
		if (tas5825m_volume[index] >= volume) {
			return index;
		}
	}
	return ARRAY_SIZE(tas5825m_volume) - 1;
}

static uint32_t tas5825m_volume_from_index(uint8_t index)
{
	if (index >= ARRAY_SIZE(tas5825m_volume))
		return tas5825m_volume[ARRAY_SIZE(tas5825m_volume) - 1];
	return tas5825m_volume[index];
}

static int tas5825m_volume_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct tas5825m_priv *priv = snd_soc_component_get_drvdata(component);
	struct soc_mixer_control *mc = (struct soc_mixer_control *)kcontrol->private_value;
	unsigned int volume_left_reg = mc->reg;
	unsigned int volume_right_reg = mc->rreg;
	uint32_t volume_left;
	uint32_t volume_right;
	int ret;

	mutex_lock(&priv->volume_lock);
	ret = regmap_bulk_read(priv->regmap,
	                       volume_left_reg,
	                       &volume_left,
	                       4);
	if (ret != 0)
		goto out;

	ret = regmap_bulk_read(priv->regmap,
	                       volume_right_reg,
	                       &volume_right,
	                       4);
	if (ret != 0)
		goto out;

	ucontrol->value.integer.value[0] = tas5825m_volume_to_index(be32_to_cpu(volume_left));
	ucontrol->value.integer.value[1] = tas5825m_volume_to_index(be32_to_cpu(volume_right));

out:
	mutex_unlock(&priv->volume_lock);
	return ret;
}

static int tas5825m_volume_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct tas5825m_priv *priv = snd_soc_component_get_drvdata(component);
	struct soc_mixer_control *mc = (struct soc_mixer_control *)kcontrol->private_value;
	unsigned int volume_left_reg = mc->reg;
	unsigned int volume_right_reg = mc->rreg;
	uint32_t volume_left = cpu_to_be32(tas5825m_volume_from_index((uint8_t) ucontrol->value.integer.value[0]));
	uint32_t volume_right = cpu_to_be32(tas5825m_volume_from_index((uint8_t) ucontrol->value.integer.value[1]));
	int ret;

	mutex_lock(&priv->volume_lock);
	ret = regmap_bulk_write(priv->regmap,
	                        volume_left_reg,
	                        &(volume_left),
	                        4);
	if (ret != 0)
		goto out;

	ret = regmap_bulk_write(priv->regmap,
	                        volume_right_reg,
	                        &(volume_right),
	                        4);
	if (ret != 0)
		goto out;

out:
	mutex_unlock(&priv->volume_lock);
	return ret;
}

/*
 * The coefficients are ordered as given in the TAS5825M process flow document:
 * b0, b1, b2, a1, a2
 */
static int tas5825m_eq_biquad_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	uinfo->count = 5 * 4;

	return 0;
}

static int tas5825m_eq_biquad_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct tas5825m_priv *priv = snd_soc_component_get_drvdata(component);
	uint32_t reg = (uint32_t) kcontrol->private_value;
	int ret = 0;
	int i;

	mutex_lock(&priv->eq_biquad_lock);
	for (i = 0; i < 5; i++) {
		ret = regmap_bulk_read(priv->regmap, reg, &(ucontrol->value.bytes.data[4 * i]), 4);
		if (ret != 0)
			goto out;

		reg = tas5825m_reg_next(reg);
	}

out:
	mutex_unlock(&priv->eq_biquad_lock);
	return ret;
}

static int tas5825m_eq_biquad_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct tas5825m_priv *priv = snd_soc_component_get_drvdata(component);
	uint32_t reg = (uint32_t) kcontrol->private_value;
	int ret = 0;
	int i;

	mutex_lock(&priv->eq_biquad_lock);
	for (i = 0; i < 5; i++) {
		ret = regmap_bulk_write(priv->regmap, reg, &(ucontrol->value.bytes.data[4 * i]), 4);
		if (ret != 0)
			goto out;

		reg = tas5825m_reg_next(reg);
	}

out:
	mutex_unlock(&priv->eq_biquad_lock);
	return ret;
}

#define TAS5825M_EQ_BIQUAD(xname, reg)\
{\
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,\
	.name = xname,\
	.info = tas5825m_eq_biquad_info,\
	.get = tas5825m_eq_biquad_get,\
	.put = tas5825m_eq_biquad_put,\
	.private_value = reg,\
}

static const DECLARE_TLV_DB_SCALE(tas5825m_speaker_volume_db_scale, -10350, 50, 0);
static const DECLARE_TLV_DB_SCALE(tas5825m_speaker_gain_db_scale, -1550, 50, 0);
static const struct snd_kcontrol_new tas5825m_controls[] = {
	SOC_DOUBLE_R_EXT_TLV("Speaker Playback Volume",
		TAS5825M_REG_DSP_VOL_LEFT,
		TAS5825M_REG_DSP_VOL_RIGHT,
		/*xshift*/ 0,
		/*xmax*/ ARRAY_SIZE(tas5825m_volume) - 1,
		/*xinvert*/ 0,
		tas5825m_volume_get,
		tas5825m_volume_put,
		tas5825m_speaker_volume_db_scale),
	SOC_SINGLE("Speaker Playback Switch",
		TAS5825M_REG_DEVICE_CTRL2,
		/*shift*/ 3,
		/*max*/ 1,
		/*invert*/ 1),
	SOC_SINGLE_TLV("Analog Gain Volume",
		TAS5825M_REG_AGAIN,
		/*shift*/ 0,
		/*max*/ 0x1f,
		/*invert*/ 1,
		tas5825m_speaker_gain_db_scale),
	SOC_SINGLE("EQ Switch",
		TAS5825M_REG_DSP_EQ_BYPASS,
		/*shift*/ 0,
		/*max*/ 1,
		/*invert*/ 1),
	SOC_SINGLE("EQ Gang Switch",
		TAS5825M_REG_DSP_EQ_GANG,
		/*shift*/ 0,
		/*max*/ 1,
		/*invert*/ 0),
	TAS5825M_EQ_BIQUAD("EQ Biquad L1", TAS5825M_REG_DSP_EQ_BQ_1_LEFT),
	TAS5825M_EQ_BIQUAD("EQ Biquad R1", TAS5825M_REG_DSP_EQ_BQ_1_RIGHT),
	TAS5825M_EQ_BIQUAD("EQ Biquad L2", TAS5825M_REG_DSP_EQ_BQ_2_LEFT),
	TAS5825M_EQ_BIQUAD("EQ Biquad R2", TAS5825M_REG_DSP_EQ_BQ_2_RIGHT),
	TAS5825M_EQ_BIQUAD("EQ Biquad L3", TAS5825M_REG_DSP_EQ_BQ_3_LEFT),
	TAS5825M_EQ_BIQUAD("EQ Biquad R3", TAS5825M_REG_DSP_EQ_BQ_3_RIGHT),
	TAS5825M_EQ_BIQUAD("EQ Biquad L4", TAS5825M_REG_DSP_EQ_BQ_4_LEFT),
	TAS5825M_EQ_BIQUAD("EQ Biquad R4", TAS5825M_REG_DSP_EQ_BQ_4_RIGHT),
	TAS5825M_EQ_BIQUAD("EQ Biquad L5", TAS5825M_REG_DSP_EQ_BQ_5_LEFT),
	TAS5825M_EQ_BIQUAD("EQ Biquad R5", TAS5825M_REG_DSP_EQ_BQ_5_RIGHT),
	TAS5825M_EQ_BIQUAD("EQ Biquad L6", TAS5825M_REG_DSP_EQ_BQ_6_LEFT),
	TAS5825M_EQ_BIQUAD("EQ Biquad R6", TAS5825M_REG_DSP_EQ_BQ_6_RIGHT),
	TAS5825M_EQ_BIQUAD("EQ Biquad L7", TAS5825M_REG_DSP_EQ_BQ_7_LEFT),
	TAS5825M_EQ_BIQUAD("EQ Biquad R7", TAS5825M_REG_DSP_EQ_BQ_7_RIGHT),
	TAS5825M_EQ_BIQUAD("EQ Biquad L8", TAS5825M_REG_DSP_EQ_BQ_8_LEFT),
	TAS5825M_EQ_BIQUAD("EQ Biquad R8", TAS5825M_REG_DSP_EQ_BQ_8_RIGHT),
	TAS5825M_EQ_BIQUAD("EQ Biquad L9", TAS5825M_REG_DSP_EQ_BQ_9_LEFT),
	TAS5825M_EQ_BIQUAD("EQ Biquad R9", TAS5825M_REG_DSP_EQ_BQ_9_RIGHT),
	TAS5825M_EQ_BIQUAD("EQ Biquad L10", TAS5825M_REG_DSP_EQ_BQ_10_LEFT),
	TAS5825M_EQ_BIQUAD("EQ Biquad R10", TAS5825M_REG_DSP_EQ_BQ_10_RIGHT),
	TAS5825M_EQ_BIQUAD("EQ Biquad L11", TAS5825M_REG_DSP_EQ_BQ_11_LEFT),
	TAS5825M_EQ_BIQUAD("EQ Biquad R11", TAS5825M_REG_DSP_EQ_BQ_11_RIGHT),
	TAS5825M_EQ_BIQUAD("EQ Biquad L12", TAS5825M_REG_DSP_EQ_BQ_12_LEFT),
	TAS5825M_EQ_BIQUAD("EQ Biquad R12", TAS5825M_REG_DSP_EQ_BQ_12_RIGHT),
	TAS5825M_EQ_BIQUAD("EQ Biquad L13", TAS5825M_REG_DSP_EQ_BQ_13_LEFT),
	TAS5825M_EQ_BIQUAD("EQ Biquad R13", TAS5825M_REG_DSP_EQ_BQ_13_RIGHT),
	TAS5825M_EQ_BIQUAD("EQ Biquad L14", TAS5825M_REG_DSP_EQ_BQ_14_LEFT),
	TAS5825M_EQ_BIQUAD("EQ Biquad R14", TAS5825M_REG_DSP_EQ_BQ_14_RIGHT),
	TAS5825M_EQ_BIQUAD("EQ Biquad L15", TAS5825M_REG_DSP_EQ_BQ_15_LEFT),
	TAS5825M_EQ_BIQUAD("EQ Biquad R15", TAS5825M_REG_DSP_EQ_BQ_15_RIGHT),
};

static const struct snd_soc_component_driver soc_component_dev_tas5825m = {
	.controls              = tas5825m_controls,
	.num_controls          = ARRAY_SIZE(tas5825m_controls),
	// .dapm_widgets          = tas5825m_dapm_widgets,
	// .num_dapm_widgets      = ARRAY_SIZE(tas5825m_dapm_widgets),
	// .dapm_routes           = tas5825m_dapm_routes,
	// .num_dapm_routes       = ARRAY_SIZE(tas5825m_dapm_routes),
	// .use_pmdown_time       = 1,
	// .endianness            = 1,
	// .non_legacy_dai_naming = 1,
};

/*
    DAI
*/

static int tas5825m_dai_hw_params(struct snd_pcm_substream *substream,
                                  struct snd_pcm_hw_params *params,
                                  struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct tas5825m_priv *priv = snd_soc_component_get_drvdata(component);
	unsigned int rate = params_rate(params);
	unsigned int width = params_width(params);
	u8 fsmode = TAS5825M_REG_SIG_CH_CTRL_FSMODE_AUTO;
	u8 word_length = TAS5825M_REG_SAP_CTRL1_WORD_LENGTH_24;
	int ret;

	dev_dbg(dai->dev, "%s() rate=%u width=%u\n", __func__, rate, width);

	switch (rate) {
	case 44100:
		fsmode = TAS5825M_REG_SIG_CH_CTRL_FSMODE_44_1KHZ;
		break;
	case 48000:
		fsmode = TAS5825M_REG_SIG_CH_CTRL_FSMODE_AUTO;
		break;
	case 96000:
		fsmode = TAS5825M_REG_SIG_CH_CTRL_FSMODE_AUTO;
		break;
	default:
		dev_err(dai->dev, "unsupported sample rate: %u\n", rate);
		return -EINVAL;
	}

	switch (width) {
	case 16:
		word_length = TAS5825M_REG_SAP_CTRL1_WORD_LENGTH_16;
		break;
	case 20:
		word_length = TAS5825M_REG_SAP_CTRL1_WORD_LENGTH_20;
		break;
	case 24:
		word_length = TAS5825M_REG_SAP_CTRL1_WORD_LENGTH_24;
		break;
	case 32:
		word_length = TAS5825M_REG_SAP_CTRL1_WORD_LENGTH_32;
		break;
	default:
		dev_err(dai->dev, "unsupported sample width: %u\n", width);
		return -EINVAL;
	}

	ret = regmap_update_bits(priv->regmap,
	                         TAS5825M_REG_SIG_CH_CTRL,
	                         TAS5825M_REG_SIG_CH_CTRL_FSMODE_MASK,
	                         fsmode);
	if (ret != 0)
		return ret;

	ret = regmap_update_bits(priv->regmap,
	                   TAS5825M_REG_SAP_CTRL1,
	                   TAS5825M_REG_SAP_CTRL1_WORD_LENGTH_MASK,
	                   word_length);
	if (ret != 0)
		return ret;

	return 0;
}

static int tas5825m_dai_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct tas5825m_priv *priv = snd_soc_component_get_drvdata(component);
	uint8_t sclk_inv = TAS5825M_REG_I2S_CTRL_SCLK_INV_NORMAL;
	uint8_t data_format = TAS5825M_REG_SAP_CTRL1_DATA_FORMAT_I2S;
	int ret;

	dev_dbg(dai->dev, "%s() fmt=0x%0x\n", __func__, fmt);

	/* clock masters */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		dev_err(dai->dev, "Invalid DAI master/slave interface\n");
		return -EINVAL;
	}

	/* signal polarity */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		sclk_inv = TAS5825M_REG_I2S_CTRL_SCLK_INV_NORMAL;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		sclk_inv = TAS5825M_REG_I2S_CTRL_SCLK_INV_INVERTED;
		break;
	default:
		dev_err(dai->dev, "Invalid DAI clock signal polarity\n");
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		data_format = TAS5825M_REG_SAP_CTRL1_DATA_FORMAT_I2S;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		data_format = TAS5825M_REG_SAP_CTRL1_DATA_FORMAT_DSP;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		data_format = TAS5825M_REG_SAP_CTRL1_DATA_FORMAT_RTJ;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		data_format = TAS5825M_REG_SAP_CTRL1_DATA_FORMAT_LTJ;
		break;
	default:
		dev_err(dai->dev, "Invalid DAI interface format\n");
		return -EINVAL;
	}

	ret = regmap_update_bits(priv->regmap,
	                         TAS5825M_REG_I2S_CTRL,
	                         TAS5825M_REG_I2S_CTRL_SCLK_INV_MASK,
	                         sclk_inv);
	if (ret != 0)
		return ret;

	ret = regmap_update_bits(priv->regmap,
	                         TAS5825M_REG_SAP_CTRL1,
	                         TAS5825M_REG_SAP_CTRL1_DATA_FORMAT_MASK,
	                         data_format);
	if (ret != 0)
		return ret;

	return ret;
}

static int tas5825m_dai_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_component *component = dai->component;
	struct tas5825m_priv *priv = snd_soc_component_get_drvdata(component);
	int ret;

	ret = regmap_update_bits(priv->regmap,
	                         TAS5825M_REG_SAP_CTRL3,
	                         TAS5825M_REG_SAP_CTRL3_LEFT_DAC_DPATH_MASK | TAS5825M_REG_SAP_CTRL3_RIGHT_DAC_DPATH_MASK,
	                         mute
	                             ? TAS5825M_REG_SAP_CTRL3_LEFT_DAC_DPATH_ZERO | TAS5825M_REG_SAP_CTRL3_RIGHT_DAC_DPATH_ZERO
	                             : TAS5825M_REG_SAP_CTRL3_LEFT_DAC_DPATH_LEFT | TAS5825M_REG_SAP_CTRL3_RIGHT_DAC_DPATH_RIGHT);
	if (ret != 0)
		return ret;

	return 0;
}

static const struct snd_soc_dai_ops tas5825m_dai_ops = {
	.hw_params      = tas5825m_dai_hw_params,
	.set_fmt        = tas5825m_dai_set_fmt,
	.digital_mute   = tas5825m_dai_mute,
};

static struct snd_soc_dai_driver tas5825m_dai[] = {
	{
		.name = "tas5825m-hifi",
		.playback = {
			.stream_name  = "Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates        = (SNDRV_PCM_RATE_44100 |\
			                 SNDRV_PCM_RATE_48000 |\
			                 SNDRV_PCM_RATE_96000),
			.formats      = (SNDRV_PCM_FMTBIT_S16_LE |\
			                 SNDRV_PCM_FMTBIT_S24_LE |\
			                 SNDRV_PCM_FMTBIT_S32_LE),
		},
		.ops = &tas5825m_dai_ops,
	},
};

/*
    Driver
*/

static int tas5825m_probe(struct device *dev,
                          struct regmap *regmap_physical)
{
	struct tas5825m_priv *priv;
	struct regmap *regmap;
	size_t i;
	int ret;

	dev_dbg(dev, "%s()\n", __func__);

	priv = devm_kzalloc(dev, sizeof(struct tas5825m_priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	regmap = devm_regmap_init(dev, NULL, priv, &tas5825m_regmap_config);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(dev, "Failed to allocate virtual register map: %d\n", ret);
		return ret;
	}

	dev_set_drvdata(dev, priv);
	for (i = 0; i < TAS5825M_NUM_SUPPLIES; i++)
		priv->supplies[i].supply = tas5825m_supply_names[i];
	priv->regmap_physical = regmap_physical;
	priv->regmap = regmap;
	priv->book_id = 0x00;
	priv->page_id = 0x00;
	mutex_init(&priv->volume_lock);
	mutex_init(&priv->eq_biquad_lock);

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(priv->supplies), priv->supplies);
	if (ret != 0) {
		dev_err(dev, "failed to get supplies: %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(priv->supplies), priv->supplies);
	if (ret) {
		dev_err(dev, "failed to enable supplies: %d\n", ret);
		return ret;
	}

	msleep(100);

	ret = regmap_register_patch(regmap_physical, tas5825m_init_sequence, ARRAY_SIZE(tas5825m_init_sequence));
	if (ret != 0)
	{
		dev_err(dev, "Failed to initialize TAS5825M: %d\n",ret);
		return ret;
	}

	msleep(100);

	ret = devm_snd_soc_register_component(dev,
	                                      &soc_component_dev_tas5825m,
	                                      tas5825m_dai, ARRAY_SIZE(tas5825m_dai));
	if (ret < 0) {
		dev_err(dev, "failed to register component: %d\n", ret);
		return ret;
	}

	return 0;
}

static int tas5825m_i2c_probe(struct i2c_client *i2c, const struct i2c_device_id *id)
{
	int ret;
	struct device *dev = &i2c->dev;
	struct regmap *regmap_physical;

	regmap_physical = devm_regmap_init_i2c(i2c, &tas5825m_regmap_physical_config);
	if (IS_ERR(regmap_physical)) {
		ret = PTR_ERR(regmap_physical);
		dev_err(dev, "Failed to allocate physical regmap: %d\n", ret);
		return ret;
	}

	return tas5825m_probe(&i2c->dev, regmap_physical);
}

static int tas5825m_remove(struct device *dev)
{
	struct tas5825m_priv *priv = dev_get_drvdata(dev);

	dev_dbg(dev, "%s()\n", __func__);

	regulator_bulk_disable(ARRAY_SIZE(priv->supplies), priv->supplies);

	return 0;
}

static int tas5825m_i2c_remove(struct i2c_client *client)
{
	return tas5825m_remove(&client->dev);
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id tas5825m_of_match[] = {
	{ .compatible = "ti,tas5825m", },
	{ }
};
MODULE_DEVICE_TABLE(of, tas5825m_of_match);
#endif

static const struct i2c_device_id tas5825m_i2c_ids[] = {
	{ "tas5825m", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tas5825m_i2c_ids);

static struct i2c_driver tas5825m_i2c_driver = {
	.driver = {
		.name = "tas5825m",
		.of_match_table = of_match_ptr(tas5825m_of_match),
	},
	.id_table = tas5825m_i2c_ids,
	.probe = tas5825m_i2c_probe,
	.remove = tas5825m_i2c_remove,
};

module_i2c_driver(tas5825m_i2c_driver);

MODULE_DESCRIPTION("ASoC TAS5825M driver");
MODULE_AUTHOR("Ali Sabil <ali.sabil@koperadev.com>");
MODULE_LICENSE("GPL");
