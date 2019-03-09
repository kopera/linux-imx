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


#define TAS5825M_NUM_SUPPLIES	2
static const char * const tas5825m_supply_names[2] = {
	"dvdd",    /* Digital power supply. Connect to 3.3-V supply. */
	"pvdd",    /* Class-D amp and analog power supply (connected). */
};

struct tas5825m_priv {
	struct regmap *regmap;
	struct regulator_bulk_data supplies[TAS5825M_NUM_SUPPLIES];
};

static bool tas5825m_is_writable_reg(struct device *device, unsigned int reg)
{
	return reg != 0x00 && reg != 0x7f;
}


static bool tas5825m_is_readable_reg(struct device *device, unsigned int reg)
{
	return reg != 0x00 && reg != 0x7f;
}


static bool tas5825m_is_volatile_reg(struct device *device, unsigned int reg)
{
	return reg == 0x00 || reg == 0x7f;
}


static const struct regmap_config tas5825m_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = 0x78,
	.writeable_reg = tas5825m_is_writable_reg,
	.readable_reg = tas5825m_is_readable_reg,
	.volatile_reg = tas5825m_is_volatile_reg,

	.cache_type = REGCACHE_RBTREE,
};


static const DECLARE_TLV_DB_SCALE(tas5825m_speaker_volume_db_scale, -10350, 50, 1);
static const struct snd_kcontrol_new tas5825m_snd_controls[] = {
	SOC_SINGLE_RANGE_TLV("Speaker Playback Volume",
	                     TAS5825M_REG_DIG_VOL,
	                     0, 0x30, 0xff, 1,
	                     tas5825m_speaker_volume_db_scale),
	SOC_SINGLE("Speaker Playback Switch",
	           TAS5825M_REG_DEVICE_CTRL2,
	           3, 1, 1),
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
	unsigned int rate = params_rate(params);
	unsigned int width = params_width(params);
	u8 fsmode = TAS5825M_REG_SIG_CH_CTRL_FSMODE_AUTO;
	u8 word_length = TAS5825M_REG_SAP_CTRL1_WORD_LENGTH_24;

	dev_dbg(codec->dev, "%s() rate=%u width=%u\n", __func__, rate, width);

	switch (rate) {
	case 44100:
		fsmode = TAS5825M_REG_SIG_CH_CTRL_FSMODE_44_1KHZ;
		break;
	case 48000:
		fsmode = TAS5825M_REG_SIG_CH_CTRL_FSMODE_48KHZ;
		break;
	case 96000:
		fsmode = TAS5825M_REG_SIG_CH_CTRL_FSMODE_96KHZ;
		break;
	default:
		dev_err(codec->dev, "unsupported sample rate: %u\n", rate);
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
		dev_err(codec->dev, "unsupported sample width: %u\n", width);
		return -EINVAL;
	}

	snd_soc_update_bits(codec, TAS5825M_REG_SIG_CH_CTRL,
	                    TAS5825M_REG_SIG_CH_CTRL_FSMODE_MASK,
	                    fsmode);
	snd_soc_update_bits(codec, TAS5825M_REG_SAP_CTRL1,
	                    TAS5825M_REG_SAP_CTRL1_WORD_LENGTH_MASK,
	                    word_length);

	return 0;
}


static int tas5825m_dai_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	uint8_t sclk_inv = TAS5825M_REG_I2S_CTRL_SCLK_INV_NORMAL;
	uint8_t data_format = TAS5825M_REG_SAP_CTRL1_DATA_FORMAT_I2S;
	int ret;

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
		sclk_inv = TAS5825M_REG_I2S_CTRL_SCLK_INV_NORMAL;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		sclk_inv = TAS5825M_REG_I2S_CTRL_SCLK_INV_INVERTED;
		break;
	default:
		dev_err(codec->dev, "Invalid DAI clock signal polarity\n");
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
		dev_err(codec->dev, "Invalid DAI interface format\n");
		return -EINVAL;
	}

	ret = snd_soc_update_bits(codec, TAS5825M_REG_I2S_CTRL,
	                          TAS5825M_REG_I2S_CTRL_SCLK_INV_MASK,
	                          sclk_inv);

	if (ret < 0)
		return ret;

	ret = snd_soc_update_bits(codec, TAS5825M_REG_SAP_CTRL1,
	                          TAS5825M_REG_SAP_CTRL1_DATA_FORMAT_MASK,
	                          data_format);

	if (ret >= 0)
		return 0;
	else
		return ret;
}


static int tas5825m_dai_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	int ret;

	ret = snd_soc_update_bits(codec, TAS5825M_REG_DEVICE_CTRL2,
	                          TAS5825M_REG_DEVICE_CTRL2_MUTE_MASK,
	                          mute ? TAS5825M_REG_DEVICE_CTRL2_MUTED : TAS5825M_REG_DEVICE_CTRL2_UNMUTED);

	if (ret >= 0)
		return 0;
	else
		return ret;
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


static int tas5825m_i2c_probe(struct i2c_client *i2c,
                          const struct i2c_device_id *id)
{
	struct tas5825m_priv *priv;
	struct device *dev = &i2c->dev;
	size_t i;
	int ret;

	dev_dbg(dev, "%s()\n", __func__);

	priv = devm_kzalloc(dev, sizeof(struct tas5825m_priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(dev, priv);

	priv->regmap = devm_regmap_init_i2c(i2c, &tas5825m_regmap_config);
	if (IS_ERR(priv->regmap)) {
		ret = PTR_ERR(priv->regmap);
		dev_err(dev, "Failed to allocate regmap: %d\n", ret);
		return ret;
	}

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

	/*
	we don't use regmap_register_patch because that would conflict with
	the register access restrictions we configure in regmap_config
	*/
	for (i = 0; i < ARRAY_SIZE(tas5825m_init_sequence); i++) {
		struct reg_sequence reg = tas5825m_init_sequence[i];
		uint8_t buffer[2] = { reg.reg, reg.def };
		ret = i2c_master_send(i2c, buffer, 2);
		if (reg.delay_us > 0) {
			udelay(reg.delay_us);
		}

		if (ret == 2)
			continue;

		dev_err(dev, "failed to initialize: %d\n", ret);
		if (ret < 0)
			return ret;
		else
			return -EIO;
	}

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
