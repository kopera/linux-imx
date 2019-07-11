/*
 * sound/soc/codecs/si468x.c -- Codec driver for SI468X chips
 *
 * Copyright (C) 2012 Innovative Converged Devices(ICD)
 * Copyright (C) 2013 Andrey Smirnov
 * Copyright (C) 2014 Bjoern Biesenbach
 * Copyright (C) 2016 Heiko Jehmlich
 *
 * Author: Bjoern Biesenbach <bjoern.biesenbach@gmail.com>; Heiko Jehmlich <hje@jecons.de>
 * Based on si468x.c of Andrey Smirnov <andrew.smirnov@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <linux/platform_device.h>


static int si468x_codec_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	return 0;
}

static int si468x_codec_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
	int rate;

	rate = params_rate(params);
	if (rate != 48000) {
		dev_err(dai->codec->dev, "Rate: %d is not supported\n", rate);
		return -EINVAL;
	}
	return 0;
}

static struct snd_soc_dai_ops si468x_dai_ops = {
	.hw_params	= si468x_codec_hw_params,
	.set_fmt	= si468x_codec_set_dai_fmt,
};

static const struct snd_soc_dapm_widget si468x_dapm_widgets[] = {
	SND_SOC_DAPM_OUTPUT("LOUT"),
	SND_SOC_DAPM_OUTPUT("ROUT"),
};

static const struct snd_soc_dapm_route si468x_dapm_routes[] = {
	{ "Capture", NULL, "LOUT" },
	{ "Capture", NULL, "ROUT" },
};

static struct snd_soc_dai_driver si468x_dai = {
	.name = "si468x-hifi",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_BE
	},
	.ops = &si468x_dai_ops,
};

static struct snd_soc_codec_driver soc_codec_dev_si468x = {
	.component_driver = {
		.dapm_widgets = si468x_dapm_widgets,
		.num_dapm_widgets = ARRAY_SIZE(si468x_dapm_widgets),
		.dapm_routes = si468x_dapm_routes,
		.num_dapm_routes = ARRAY_SIZE(si468x_dapm_routes),
	},
};

static int si468x_probe(struct platform_device *pdev)
{
	return snd_soc_register_codec(&pdev->dev, &soc_codec_dev_si468x, &si468x_dai, 1);
}

static int si468x_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static const struct of_device_id si468x_of_match[] = {
	{ .compatible = "silabs,si468x-codec", },
	{ }
};
MODULE_DEVICE_TABLE(of, si468x_of_match);

static struct platform_driver si468x_platform_driver = {
	.probe = si468x_probe,
	.remove = si468x_remove,
	.driver = {
		.name = "si468x-codec",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(si468x_of_match),
	},
};

module_platform_driver(si468x_platform_driver);

MODULE_AUTHOR("Bjoern Biesenbach <bjoern@bjoern-b.de>");
MODULE_DESCRIPTION("ASoC Si468X codec driver");
MODULE_LICENSE("GPL");