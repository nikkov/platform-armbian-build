/*
 * Driver for clocks board for i2s bus
 *
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

#include <linux/clk.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-component.h>

struct i2s_clock_board_priv {
    struct device *dev;
    // mute pin, =0 if not used
    struct gpio_desc *pd_gpio;
    bool inverse_mute;
};


static const struct snd_soc_component_driver soc_codec_i2s_clock_board = {
};

static int i2s_clock_board_trigger(struct snd_pcm_substream *substream, int cmd,
             struct snd_soc_dai *dai)
{
    struct snd_soc_component *component = dai->component;
    struct i2s_clock_board_priv *priv = snd_soc_component_get_drvdata(component);
    switch (cmd) {
        case SNDRV_PCM_TRIGGER_START:
        case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
        case SNDRV_PCM_TRIGGER_RESUME:
            dev_dbg(priv->dev, "i2s_clock_board: %s, component %s start\n", __func__, component->name);
            if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
                if (priv->pd_gpio)
                    gpiod_set_value(priv->pd_gpio, priv->inverse_mute ? 1 : 0);
            }
            else {
            }
        break;

        case SNDRV_PCM_TRIGGER_STOP:
        case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
        case SNDRV_PCM_TRIGGER_SUSPEND:
            dev_dbg(priv->dev, "i2s_clock_board: %s, component %s stop\n", __func__, component->name);
            if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
                if (priv->pd_gpio)
                    gpiod_set_value(priv->pd_gpio, priv->inverse_mute ? 0 : 1);
            }
            else {
            }
        break;

        default:
        return -EINVAL;
    }

    return 0;
}

static const struct snd_soc_dai_ops i2s_clock_board_dai_ops = {
    .trigger    = i2s_clock_board_trigger,
};

static struct snd_soc_dai_driver i2s_clock_board_dai = {
    .name               = "i2s_clock_board",
    .playback = {
        .stream_name    = "Playback",
        .channels_min   = 2,
        .channels_max   = 8,
        .rates          = SNDRV_PCM_RATE_KNOT,
        .formats        = (SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S24_LE |
		       SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_DSD_U32_LE),
    },
    .capture = {
        .stream_name    = "Capture",
        .channels_min   = 2,
        .channels_max   = 8,
        .rates          = SNDRV_PCM_RATE_KNOT,
        .formats        = (SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S24_LE |
		       SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_DSD_U32_LE),
    },
    .ops                = &i2s_clock_board_dai_ops,
};

static const struct of_device_id i2s_clock_board_dt_ids[] = {
    { .compatible = "custom,i2s_clock_board", },
    { }
};
MODULE_DEVICE_TABLE(of, i2s_clock_board_dt_ids);

static int i2s_clock_board_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct i2s_clock_board_priv *priv;

    dev_dbg(dev, "%s\n", __func__);
    priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
    if (!priv)
        return -ENOMEM;

    dev_set_drvdata(dev, priv);
    priv->dev = &pdev->dev;
	
    priv->pd_gpio = devm_gpiod_get(dev, "mute", GPIOD_OUT_HIGH);
    if (IS_ERR(priv->pd_gpio)) {
        priv->pd_gpio = 0;
        dev_dbg(dev, "%s: mute not used\n", __func__);
    }
    else {
        if (of_property_read_bool(dev->of_node, "inversion-mute"))
            priv->inverse_mute = true;
        else
            priv->inverse_mute = false;
        gpiod_set_value(priv->pd_gpio, priv->inverse_mute ? 0 : 1);
        dev_dbg(dev, "%s: mute used, inverse flag = %d\n", __func__, (int)priv->inverse_mute);
    }
	
    return snd_soc_register_component(dev, &soc_codec_i2s_clock_board,
		&i2s_clock_board_dai, 1);
}

static struct platform_driver i2s_clock_board_driver = {
    .probe              = i2s_clock_board_probe,
    .driver             = {
        .name           = "i2s_clock_board",
        .of_match_table = of_match_ptr(i2s_clock_board_dt_ids),
    },
};

module_platform_driver(i2s_clock_board_driver);

MODULE_DESCRIPTION("ASoC external clock board for I2S bus");
MODULE_LICENSE("GPL v2");