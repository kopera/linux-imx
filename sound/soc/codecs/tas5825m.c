/*
 * Driver for the TAS5825M Audio Amplifier
 *
 * Author: Andy Liu <andy-liu@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
// #include <linux/regulator/consumer.h>

#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/initval.h>

#include "tas5825m.h"


#define TAS5825M_RATES	     (SNDRV_PCM_RATE_48000)
#define TAS5825M_FORMATS     (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
                              SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

#define TAS5825M_REG_00      (0x00)
#define TAS5825M_REG_03      (0x03)
#define TAS5825M_REG_0C      (0x0c)
#define TAS5825M_REG_0D      (0x0d)
#define TAS5825M_REG_0E      (0x0e)
#define TAS5825M_REG_0F      (0x0f)
#define TAS5825M_REG_10      (0x10)
#define TAS5825M_REG_11      (0x11)
#define TAS5825M_REG_12      (0x12)
#define TAS5825M_REG_13      (0x13)
#define TAS5825M_REG_35      (0x35)
#define TAS5825M_REG_7F      (0x7f)

#define TAS5825M_PAGE_00     (0x00)
#define TAS5825M_PAGE_0B     (0x0b)

#define TAS5825M_BOOK_00     (0x00)
#define TAS5825M_BOOK_8C     (0x8c)


// static const char * const tas5825m_supply_names[] = {
// 	"dvdd",		/* Digital power supply. Connect to 3.3-V supply. */
// 	"pvdd",		/* Class-D amp and analog power supply (connected). */
// };

// #define TAS5825M_NUM_SUPPLIES	ARRAY_SIZE(tas5825m_supply_names)


struct tas5825m_priv {
	struct mutex lock;

	struct regmap *regmap;
	int volume;
	// struct regulator_bulk_data supplies[TAS5825M_NUM_SUPPLIES];
};

static int tas5825m_vol_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type   = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count  = 1;

	uinfo->value.integer.min  = TAS5825M_VOLUME_MIN;
	uinfo->value.integer.max  = TAS5825M_VOLUME_MAX;
	uinfo->value.integer.step = 1;

	return 0;
}

static int tas5825m_vol_locked_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct tas5825m_priv *tas5825m = snd_soc_codec_get_drvdata(codec);

	mutex_lock(&tas5825m->lock);
	ucontrol->value.integer.value[0] = tas5825m->volume;
	mutex_unlock(&tas5825m->lock);

	return 0;
}

static inline int get_volume_index(int volume)
{
	int index;

	index = volume;

	if (index < TAS5825M_VOLUME_MIN)
		index = TAS5825M_VOLUME_MIN;

	if (index > TAS5825M_VOLUME_MAX)
		index = TAS5825M_VOLUME_MAX;

	return index;
}

static void tas5825m_set_volume(struct snd_soc_codec *codec, int volume)
{
	unsigned int index;
	uint32_t volume_hex;
	uint8_t byte4;
	uint8_t byte3;
	uint8_t byte2;
	uint8_t byte1;

	index = get_volume_index(volume);
	volume_hex = tas5825m_volume[index];

	byte4 = ((volume_hex >> 24) & 0xFF);
	byte3 = ((volume_hex >> 16) & 0xFF);
	byte2 = ((volume_hex >> 8)	& 0xFF);
	byte1 = ((volume_hex >> 0)	& 0xFF);

	//w 98 00 00
	snd_soc_write(codec, TAS5825M_REG_00, TAS5825M_PAGE_00);
	//w 98 7f 8c
	snd_soc_write(codec, TAS5825M_REG_7F, TAS5825M_BOOK_8C);
	//w 98 00 0b
	snd_soc_write(codec, TAS5825M_REG_00, TAS5825M_PAGE_0B);
	//w 98 0c xx xx xx xx
	snd_soc_write(codec, TAS5825M_REG_0C, byte4);
	snd_soc_write(codec, TAS5825M_REG_0D, byte3);
	snd_soc_write(codec, TAS5825M_REG_0E, byte2);
	snd_soc_write(codec, TAS5825M_REG_0F, byte1);
	//w 98 10 xx xx xx xx
	snd_soc_write(codec, TAS5825M_REG_10, byte4);
	snd_soc_write(codec, TAS5825M_REG_11, byte3);
	snd_soc_write(codec, TAS5825M_REG_12, byte2);
	snd_soc_write(codec, TAS5825M_REG_13, byte1);
}

static int tas5825m_vol_locked_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct tas5825m_priv *tas5825m = snd_soc_codec_get_drvdata(codec);

	mutex_lock(&tas5825m->lock);

	tas5825m->volume = ucontrol->value.integer.value[0];
	tas5825m_set_volume(codec, tas5825m->volume);

	mutex_unlock(&tas5825m->lock);

	return 0;
}

static const struct snd_kcontrol_new tas5825m_snd_controls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Playback Volume",
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = tas5825m_vol_info,
		.get = tas5825m_vol_locked_get,
		.put = tas5825m_vol_locked_put,
	}
};

static const struct snd_soc_dapm_widget tas5825m_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("IN"),
	SND_SOC_DAPM_OUTPUT("OUT")
};

static const struct snd_soc_dapm_route tas5825m_audio_map[] = {
	{ "OUT", NULL, "IN" },
};

static int tas5825m_codec_probe(struct snd_soc_codec *codec)
{
	// struct tas5825m_priv *tas5825m = snd_soc_codec_get_drvdata(codec);
	// int ret;

	// ret = regulator_bulk_enable(ARRAY_SIZE(tas5825m->supplies), tas5825m->supplies);
	// if (ret != 0) {
	// 	dev_err(codec->dev, "failed to enable supplies: %d\n", ret);
	// 	return ret;
	// }

	return 0;
}

static int tas5825m_codec_remove(struct snd_soc_codec *codec)
{
	// struct tas5825m_priv *tas5825m = snd_soc_codec_get_drvdata(codec);
	// int ret;

	// ret = regulator_bulk_disable(ARRAY_SIZE(tas5825m->supplies), tas5825m->supplies);
	// if (ret < 0) {
	// 	dev_err(codec->dev, "failed to disable supplies: %d\n", ret);
	// }

	// return ret;
	return 0;
};

static struct snd_soc_codec_driver soc_codec_tas5825m = {
	.probe = tas5825m_codec_probe,
	.remove = tas5825m_codec_remove,

	.component_driver = {
		.controls = tas5825m_snd_controls,
		.num_controls = ARRAY_SIZE(tas5825m_snd_controls),
		.dapm_widgets = tas5825m_dapm_widgets,
		.num_dapm_widgets = ARRAY_SIZE(tas5825m_dapm_widgets),
		.dapm_routes = tas5825m_audio_map,
		.num_dapm_routes = ARRAY_SIZE(tas5825m_audio_map),
	},
};

static int tas5825m_mute(struct snd_soc_dai *dai, int mute)
{
	u8 reg03_value = 0;
	u8 reg35_value = 0;
	struct snd_soc_codec *codec = dai->codec;

	if (mute) {
		//mute both left & right channels
		reg03_value = 0x0b;
		reg35_value = 0x00;
	} else {
		//umute
		reg03_value = 0x03;
		reg35_value = 0x11;
	}

	snd_soc_write(codec, TAS5825M_REG_00, TAS5825M_PAGE_00);
	snd_soc_write(codec, TAS5825M_REG_7F, TAS5825M_BOOK_00);
	snd_soc_write(codec, TAS5825M_REG_00, TAS5825M_PAGE_00);
	snd_soc_write(codec, TAS5825M_REG_03, reg03_value);
	snd_soc_write(codec, TAS5825M_REG_35, reg35_value);

	return 0;
}

static const struct snd_soc_dai_ops tas5825m_dai_ops = {
	// .hw_params	= tas5825m_hw_params,
	// .set_fmt	= tas5825m_set_dai_fmt,
	// .set_tdm_slot	= tas5825m_set_dai_tdm_slot,
	.digital_mute = tas5825m_mute,
};

static struct snd_soc_dai_driver tas5825m_dai = {
	.name = "tas5825m-amplifier",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = TAS5825M_RATES,
		.formats = TAS5825M_FORMATS,
	},
	.ops = &tas5825m_dai_ops,
};

static const struct regmap_config tas5825m_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
};

static int tas5825m_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct tas5825m_priv *tas5825m;
	int ret;
	int i;

	tas5825m = devm_kzalloc(dev, sizeof(*tas5825m), GFP_KERNEL);
	if (!tas5825m)
		return -ENOMEM;

	mutex_init(&tas5825m->lock);
	tas5825m->regmap = devm_regmap_init_i2c(client, &tas5825m_regmap_config);
	if (IS_ERR(tas5825m->regmap)) {
		ret = PTR_ERR(tas5825m->regmap);
		dev_err(dev, "failed to allocate register map: %d\n", ret);
		return ret;
	}
	tas5825m->volume = 100;         //100, -10dB

	ret = regmap_register_patch(tas5825m->regmap, tas5825m_init_sequence, ARRAY_SIZE(tas5825m_init_sequence));
	if (ret != 0) {
		dev_err(dev, "failed to initialize TAS5825M: %d\n",ret);
		return ret;
	}

	// for (i = 0; i < TAS5825M_NUM_SUPPLIES; i++)
	// 	tas5825m->supplies[i].supply = tas5825m_supply_names[i];

	// ret = devm_regulator_bulk_get(dev, TAS5825M_NUM_SUPPLIES, tas5825m->supplies);
	// if (ret != 0) {
	// 	dev_err(dev, "failed to request supplies: %d\n", ret);
	// 	return ret;
	// }

	dev_set_drvdata(dev, tas5825m);

	ret = snd_soc_register_codec(dev, &soc_codec_tas5825m, &tas5825m_dai, 1);
	if (ret < 0) {
		dev_err(dev, "failed to register codec: %d\n", ret);
		return ret;
	}

	return 0;
}

static int tas5825m_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);

	return 0;
}

static const struct i2c_device_id tas5825m_id[] = {
	{ "tas5825m", },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tas5825m_id);

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id tas5825m_of_match[] = {
	{ .compatible = "ti,tas5825m", },
	{ }
};
MODULE_DEVICE_TABLE(of, tas5825m_of_match);
#endif

static struct i2c_driver tas5825m_i2c_driver = {
	.probe = tas5825m_probe,
	.remove = tas5825m_remove,
	.id_table = tas5825m_id,
	.driver = {
		.name = "tas5825m",
		.of_match_table = of_match_ptr(tas5825m_of_match),
	},
};

module_i2c_driver(tas5825m_i2c_driver);

MODULE_AUTHOR("Andy Liu <andy-liu@ti.com>");
MODULE_DESCRIPTION("TAS5825M Audio Amplifier Driver");
MODULE_LICENSE("GPL v2");