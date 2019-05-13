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


#define TAS5825M_NUM_SUPPLIES	2
static const char * const tas5825m_supply_names[2] = {
	"dvdd",    /* Digital power supply. Connect to 3.3-V supply. */
	"pvdd",    /* Class-D amp and analog power supply (connected). */
};


struct tas5825m_priv {
	struct mutex  lock;

	struct regmap *regmap;
	struct regulator_bulk_data supplies[TAS5825M_NUM_SUPPLIES];

	size_t volume_l_index;
	size_t volume_r_index;

	bool muted;
};


static int tas5825m_speaker_volume_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct tas5825m_priv *priv = snd_soc_codec_get_drvdata(codec);

	mutex_lock(&priv->lock);
	ucontrol->value.integer.value[0] = priv->volume_l_index;
	ucontrol->value.integer.value[1] = priv->volume_r_index;
	mutex_unlock(&priv->lock);

	dev_dbg(codec->dev, "%s(): volume=(%ld, %ld)\n", __func__, priv->volume_l_index, priv->volume_r_index);

	return 0;
}

static int tas5825m_speaker_volume_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct tas5825m_priv *priv = snd_soc_codec_get_drvdata(codec);
	size_t l = ucontrol->value.integer.value[0];
	size_t r = ucontrol->value.integer.value[1];

	size_t volume_l_index = clamp(l, TAS5825M_VOLUME_MIN, TAS5825M_VOLUME_MAX);
	uint32_t volume_l = tas5825m_volume[volume_l_index];
	uint8_t volume_l4 = ((volume_l >> 24) & 0xff);
	uint8_t volume_l3 = ((volume_l >> 16) & 0xff);
	uint8_t volume_l2 = ((volume_l >>  8) & 0xff);
	uint8_t volume_l1 = ((volume_l >>  0) & 0xff);

	size_t volume_r_index = clamp(r, TAS5825M_VOLUME_MIN, TAS5825M_VOLUME_MAX);
	uint32_t volume_r = tas5825m_volume[volume_r_index];
	uint8_t volume_r4 = ((volume_r >> 24) & 0xff);
	uint8_t volume_r3 = ((volume_r >> 16) & 0xff);
	uint8_t volume_r2 = ((volume_r >>  8) & 0xff);
	uint8_t volume_r1 = ((volume_r >>  0) & 0xff);

	dev_dbg(codec->dev, "%s(volume=(%lu, %lu))\n", __func__, volume_l_index, volume_r_index);

	mutex_lock(&priv->lock);

	snd_soc_write(codec, TAS5825M_REG_PAGE_CTRL, 0x00);
	snd_soc_write(codec, TAS5825M_REG_BOOK_CTRL, 0x8c);
	snd_soc_write(codec, TAS5825M_REG_PAGE_CTRL, 0x0b);

	snd_soc_write(codec, 0x0c, volume_l4);
	snd_soc_write(codec, 0x0d, volume_l3);
	snd_soc_write(codec, 0x0e, volume_l2);
	snd_soc_write(codec, 0x0f, volume_l1);
	priv->volume_l_index = volume_l_index;

	snd_soc_write(codec, 0x10, volume_r4);
	snd_soc_write(codec, 0x11, volume_r3);
	snd_soc_write(codec, 0x12, volume_r2);
	snd_soc_write(codec, 0x13, volume_r1);
	priv->volume_r_index = volume_r_index;

	mutex_unlock(&priv->lock);

	return 1;
}

static int tas5825m_speaker_switch_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct tas5825m_priv *priv = snd_soc_codec_get_drvdata(codec);

	mutex_lock(&priv->lock);
	ucontrol->value.integer.value[0] = !priv->muted;
	mutex_unlock(&priv->lock);

	return 0;
}

static int tas5825m_speaker_switch_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct tas5825m_priv *priv = snd_soc_codec_get_drvdata(codec);

	int value = ucontrol->value.integer.value[0];
	bool muted = value == 0;

	mutex_lock(&priv->lock);

	snd_soc_write(codec, TAS5825M_REG_PAGE_CTRL, TAS5825M_PAGE_00);
	snd_soc_write(codec, TAS5825M_REG_BOOK_CTRL, TAS5825M_BOOK_00);
	snd_soc_write(codec, TAS5825M_REG_PAGE_CTRL, TAS5825M_PAGE_00);
	snd_soc_update_bits(codec, TAS5825M_REG_DEVICE_CTRL2,
	                    TAS5825M_MUTE_MASK,
	                    muted ? TAS5825M_MUTE_MUTED : TAS5825M_MUTE_UNMUTED);

	priv->muted = muted;

	mutex_unlock(&priv->lock);

	return 1;
}

static const DECLARE_TLV_DB_SCALE(tas5825m_speaker_volume_db_scale, -10350, 50, 1);
static const struct snd_kcontrol_new tas5825m_snd_controls[] = {
	SOC_DOUBLE_R_EXT_TLV("Speaker Playback Volume",
	                     0x0c, 0x10, 0, TAS5825M_VOLUME_MAX, 0,
						 tas5825m_speaker_volume_get, tas5825m_speaker_volume_put,
	                     tas5825m_speaker_volume_db_scale),
	SOC_SINGLE_EXT("Speaker Playback Switch",
	               TAS5825M_REG_DEVICE_CTRL2, 3, 1, 0,
	               tas5825m_speaker_switch_get, tas5825m_speaker_switch_put),
};

static struct snd_soc_codec_driver soc_codec_dev_tas5825m = {
	.component_driver = {
		.controls         = tas5825m_snd_controls,
		.num_controls     = ARRAY_SIZE(tas5825m_snd_controls),
	},
};


static int tas5825m_dai_hw_params(struct snd_pcm_substream *substream,
                             struct snd_pcm_hw_params *params,
                             struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct tas5825m_priv *priv = snd_soc_codec_get_drvdata(codec);
	unsigned int rate = params_rate(params);
	unsigned int width = params_width(params);
	u8 fsmode = TAS5825M_FSMODE_AUTO;
	u8 word_length = TAS5825M_WORD_LENGTH_24;

	dev_dbg(codec->dev, "%s() rate=%u width=%u\n", __func__, rate, width);

	switch (rate) {
	case 44100:
		fsmode = TAS5825M_FSMODE_44_1KHZ;
		break;
	case 48000:
		fsmode = TAS5825M_FSMODE_48KHZ;
		break;
	case 96000:
		fsmode = TAS5825M_FSMODE_96KHZ;
		break;
	default:
		dev_err(codec->dev, "unsupported sample rate: %u\n", rate);
		return -EINVAL;
	}

	switch (width) {
	case 16:
		word_length = TAS5825M_WORD_LENGTH_16;
		break;
	case 20:
		word_length = TAS5825M_WORD_LENGTH_20;
		break;
	case 24:
		word_length = TAS5825M_WORD_LENGTH_24;
		break;
	case 32:
		word_length = TAS5825M_WORD_LENGTH_32;
		break;
	default:
		dev_err(codec->dev, "unsupported sample width: %u\n", width);
		return -EINVAL;
	}

	mutex_lock(&priv->lock);

	snd_soc_write(codec, TAS5825M_REG_PAGE_CTRL, TAS5825M_PAGE_00);
	snd_soc_write(codec, TAS5825M_REG_BOOK_CTRL, TAS5825M_BOOK_00);
	snd_soc_write(codec, TAS5825M_REG_PAGE_CTRL, TAS5825M_PAGE_00);
	snd_soc_update_bits(codec, TAS5825M_REG_SIG_CH_CTRL,
	                    TAS5825M_FSMODE_MASK,
	                    fsmode);
	snd_soc_update_bits(codec, TAS5825M_REG_SAP_CTRL1,
	                    TAS5825M_WORD_LENGTH_MASK,
	                    word_length);

	mutex_unlock(&priv->lock);

	return 0;
}


static int tas5825m_dai_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	struct tas5825m_priv *priv = snd_soc_codec_get_drvdata(codec);
	u8 sclk_inv = TAS5825M_SCLK_INV_NORMAL;
	u8 data_format = TAS5825M_DATA_FORMAT_I2S;

	dev_dbg(codec->dev, "%s() fmt=0x%0x\n", __func__, fmt);

	/* clock masters */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		dev_err(codec->dev, "Invalid DAI master/slave interface\n");
		return -EINVAL;
	}

	/* signal polarity */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		sclk_inv = TAS5825M_SCLK_INV_NORMAL;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		sclk_inv = TAS5825M_SCLK_INV_INVERTED;
		break;
	default:
		dev_err(codec->dev, "Invalid DAI clock signal polarity\n");
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		data_format = TAS5825M_DATA_FORMAT_I2S;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		data_format = TAS5825M_DATA_FORMAT_DSP;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		data_format = TAS5825M_DATA_FORMAT_RTJ;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		data_format = TAS5825M_DATA_FORMAT_LTJ;
		break;
	default:
		dev_err(codec->dev, "Invalid DAI interface format\n");
		return -EINVAL;
	}

	mutex_lock(&priv->lock);

	snd_soc_write(codec, TAS5825M_REG_PAGE_CTRL, TAS5825M_PAGE_00);
	snd_soc_write(codec, TAS5825M_REG_BOOK_CTRL, TAS5825M_BOOK_00);
	snd_soc_write(codec, TAS5825M_REG_PAGE_CTRL, TAS5825M_PAGE_00);
	snd_soc_update_bits(codec, TAS5825M_REG_I2S_CTRL,
	                    TAS5825M_SCLK_INV_MASK,
	                    sclk_inv);
	snd_soc_update_bits(codec, TAS5825M_REG_SAP_CTRL1,
	                    TAS5825M_DATA_FORMAT_MASK,
	                    data_format);

	mutex_unlock(&priv->lock);

	return 0;
}

static int tas5825m_dai_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	struct tas5825m_priv *priv = snd_soc_codec_get_drvdata(codec);

	mutex_lock(&priv->lock);

	snd_soc_write(codec, TAS5825M_REG_PAGE_CTRL, TAS5825M_PAGE_00);
	snd_soc_write(codec, TAS5825M_REG_BOOK_CTRL, TAS5825M_BOOK_00);
	snd_soc_write(codec, TAS5825M_REG_PAGE_CTRL, TAS5825M_PAGE_00);
	snd_soc_update_bits(codec, TAS5825M_REG_DEVICE_CTRL2,
	                    TAS5825M_MUTE_MASK,
	                    mute ? TAS5825M_MUTE_MUTED : TAS5825M_MUTE_UNMUTED);

	mutex_unlock(&priv->lock);

	return 0;
}

static const struct snd_soc_dai_ops tas5825m_speaker_dai_ops = {
	.hw_params      = tas5825m_dai_hw_params,
	.set_fmt        = tas5825m_dai_set_fmt,
	.digital_mute   = tas5825m_dai_mute,
};

static struct snd_soc_dai_driver tas5825m_dai[] = {
	{
		.name = "tas5825m-amplifier",
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
		.ops = &tas5825m_speaker_dai_ops,
	},
};

static bool tas5825m_is_volatile_reg(struct device *dev, unsigned int reg)
{
	return true;
}

static bool tas5825m_is_writable_reg(struct device *dev, unsigned int reg)
{
	return true;
}

static const struct regmap_config tas5825m_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.writeable_reg = tas5825m_is_writable_reg,
	.volatile_reg = tas5825m_is_volatile_reg,

	.cache_type = REGCACHE_RBTREE,
	.max_register = 128,
};

static int tas5825m_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct tas5825m_priv *priv;
	struct device *dev = &client->dev;
	int i, ret;

	dev_dbg(dev, "%s()\n", __func__);

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(dev, priv);

	mutex_init(&priv->lock);
	priv->volume_l_index = TAS5825M_VOLUME_DEFAULT_INDEX;
	priv->volume_r_index = TAS5825M_VOLUME_DEFAULT_INDEX;
	priv->muted = false;
	priv->regmap = devm_regmap_init_i2c(client, &tas5825m_regmap_config);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	for (i = 0; i < TAS5825M_NUM_SUPPLIES; i++)
		priv->supplies[i].supply = tas5825m_supply_names[i];

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

	ret = regmap_register_patch(priv->regmap,
	                            tas5825m_init_sequence, ARRAY_SIZE(tas5825m_init_sequence));
	if (ret != 0) {
		dev_err(dev, "failed to start initialization: %d\n", ret);
		return ret;
	}

	ret = regmap_async_complete(priv->regmap);
	if (ret != 0) {
		dev_err(dev, "failed to complete initialization: %d\n", ret);
		return ret;
	}

	msleep(500);

	ret = snd_soc_register_codec(dev,
	                             &soc_codec_dev_tas5825m,
	                             tas5825m_dai, ARRAY_SIZE(tas5825m_dai));
	if (ret < 0) {
		dev_err(dev, "failed to register codec: %d\n", ret);
		return ret;
	}

	return 0;
}

static int tas5825m_i2c_remove(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct tas5825m_priv *priv = dev_get_drvdata(dev);

	dev_dbg(dev, "%s()\n", __func__);

	snd_soc_unregister_codec(dev);
	regulator_bulk_disable(ARRAY_SIZE(priv->supplies), priv->supplies);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id tas5825m_of_match[] = {
	{ .compatible = "ti,tas5825m", },
	{ }
};
MODULE_DEVICE_TABLE(of, tas5825m_of_match);
#endif

static const struct i2c_device_id tas5825m_i2c_id[] = {
	{ "tas5825m", },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tas5825m_i2c_id);

static struct i2c_driver tas5825m_i2c_driver = {
	.driver = {
		.name = "tas5825m",
		.of_match_table = of_match_ptr(tas5825m_of_match),
	},
	.probe = tas5825m_i2c_probe,
	.remove = tas5825m_i2c_remove,
	.id_table = tas5825m_i2c_id,
};

module_i2c_driver(tas5825m_i2c_driver);

MODULE_DESCRIPTION("ASoC TAS5825M driver");
MODULE_AUTHOR("Andy Liu <andy-liu@ti.com>");
MODULE_LICENSE("GPL v2");