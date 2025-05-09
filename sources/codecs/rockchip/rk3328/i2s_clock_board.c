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

#if (1)
#define DBGOUT(msg...)		do { printk(msg); } while (0)
#else
#define DBGOUT(msg...)		do {} while (0)
#endif


struct i2s_clock_board_priv {
    struct device *dev;
    // child of clkin
    struct clk *i2s_clk;
    // clkin
    struct clk *i2s_pclk;
    // master clock ration
    unsigned long mclk_fs;
    // mute pin, =0 if not used
    struct gpio_desc *pd_gpio;
    bool inverse_mute;
};

static int i2s_clock_board_soc_probe(struct snd_soc_component *component)
{
    DBGOUT("i2s_clock_board: %s\n", __func__);
    return 0;
}

static void i2s_clock_board_soc_remove(struct snd_soc_component *component)
{
    DBGOUT("i2s_clock_board: %s\n", __func__);
}

static int i2s_clock_board_soc_suspend(struct snd_soc_component *component)
{
    DBGOUT("i2s_clock_board: %s\n", __func__);
    return 0;
}

static int i2s_clock_board_soc_resume(struct snd_soc_component *component)
{
    DBGOUT("i2s_clock_board: %s\n", __func__);
    return 0;
}

static const struct snd_soc_component_driver soc_codec_i2s_clock_board = {
    .probe = i2s_clock_board_soc_probe,
    .remove = i2s_clock_board_soc_remove,
    .suspend = i2s_clock_board_soc_suspend,
    .resume = i2s_clock_board_soc_resume,
};

static int i2s_clock_board_set_dai_fmt(struct snd_soc_dai *codec_dai,
        unsigned int format)
{
    DBGOUT("i2s_clock_board: %s\n", __func__);
    return 0;
}


static int set_clock(struct i2s_clock_board_priv *priv, unsigned long rate)
{
    int ret;
    DBGOUT("i2s_clock_board: set_clock: %lu\n", rate);
    ret = clk_set_rate(priv->i2s_pclk, rate);
    if(ret)
        dev_err(priv->dev, "clk_set_rate %lu error\n", rate);
    else
        DBGOUT("i2s_clock_board: %s clk_set_rate %lu OK\n", __func__, rate);

    ret = clk_set_parent(priv->i2s_clk, priv->i2s_pclk);
    if(ret)
        dev_err(priv->dev, "clk_set_parent error\n");
    else
        DBGOUT("i2s_clock_board: %s clk_set_parent OK\n", __func__);

    return ret;
}


static int i2s_clock_board_hw_params(struct snd_pcm_substream *substream,
    struct snd_pcm_hw_params *params,
    struct snd_soc_dai *dai)
{
    unsigned long rate = params_rate(params);
    unsigned long mclk;
    struct snd_soc_component *component = dai->component;
    struct i2s_clock_board_priv *priv = snd_soc_component_get_drvdata(component);
    DBGOUT("i2s_clock_board: %s, physical_width=%d, rate=%d, width=%d\n", __func__, (int)params_physical_width(params), (int)rate, (int)params_width(params));

    switch(rate)
    {
        case 44100:
        case 88200:
        case 176400:
            mclk = priv->mclk_fs * 44100;
            break;
        case 48000:
        case 96000:
        case 192000:
        case 384000:
            mclk = priv->mclk_fs * 48000;
            break;
    }

    return set_clock(priv, mclk);
}

static int i2s_clock_board_trigger(struct snd_pcm_substream *substream, int cmd,
             struct snd_soc_dai *dai)
{
    struct snd_soc_component *component = dai->component;
    struct i2s_clock_board_priv *priv = snd_soc_component_get_drvdata(component);
    switch (cmd) {
        case SNDRV_PCM_TRIGGER_START:
        case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
        case SNDRV_PCM_TRIGGER_RESUME:
            DBGOUT("i2s_clock_board: %s, component %s start\n", __func__, component->name);
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
            DBGOUT("i2s_clock_board: %s, component %s stop\n", __func__, component->name);
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
    .set_fmt    = i2s_clock_board_set_dai_fmt,
    .hw_params  = i2s_clock_board_hw_params,
    .trigger    = i2s_clock_board_trigger,
};

#define SNDRV_PCM_RATE_44100_192000 (SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 | \
        SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 | \
        SNDRV_PCM_RATE_192000)

static struct snd_soc_dai_driver i2s_clock_board_dai = {
    .name               = "i2s_clock_board",
    .playback = {
        .stream_name    = "Playback",
        .channels_min   = 2,
        .channels_max   = 8,
        .rates          = SNDRV_PCM_RATE_44100_192000 | SNDRV_PCM_RATE_KNOT,
        .rate_min       = 44100,
        .rate_max       = 384000,
        .formats        = SNDRV_PCM_FMTBIT_S24_LE,
    },
    .capture = {
        .stream_name    = "Capture",
        .channels_min   = 2,
        .channels_max   = 2,
        .rates          = SNDRV_PCM_RATE_44100_192000,
        .formats        = SNDRV_PCM_FMTBIT_S24_LE,
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
    u32 mclk_fs;
    struct clk *i2s_clk;

    DBGOUT("i2s_clock_board: %s\n", __func__);
    priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
    if (!priv)
        return -ENOMEM;

    dev_set_drvdata(dev, priv);
    priv->dev = &pdev->dev;

    i2s_clk = devm_clk_get(&pdev->dev, "i2s_clk");
    if (IS_ERR(i2s_clk)) {
        dev_err(&pdev->dev, "Can't retrieve i2s clock\n");
        return PTR_ERR(i2s_clk);
    }
    else
        DBGOUT("i2s_clock_board: %s get_clock %s OK\n", __func__, "i2s_clk");

    priv->i2s_clk = clk_get_parent(i2s_clk);
    if (IS_ERR(priv->i2s_clk)) {
        dev_err(&pdev->dev, "Can't retrieve parent of i2s clock\n");
        return PTR_ERR(priv->i2s_clk);
    }
    else
        DBGOUT("i2s_clock_board: %s clk_get_parent OK\n", __func__);

    priv->i2s_pclk = devm_clk_get(&pdev->dev, "i2s_pclk");
    if (IS_ERR(priv->i2s_pclk)) {
        dev_err(&pdev->dev, "Can't retrieve external i2s clock\n");
        return PTR_ERR(priv->i2s_pclk);
    }
    else
        DBGOUT("i2s_clock_board: %s get_clock %s OK\n", __func__, "i2s_pclk");

    if(of_property_read_u32(pdev->dev.of_node, "mclk-fs", &mclk_fs) == 0)
        DBGOUT("i2s_clock_board: %s read mclk-fs %u OK\n", __func__, mclk_fs);
    else
    {
        DBGOUT("i2s_clock_board: %s read mclk-fs ERROR set to default value 256\n", __func__);
        mclk_fs = 256;
    }
    priv->mclk_fs = mclk_fs;

    priv->pd_gpio = devm_gpiod_get(dev, "mute", GPIOD_OUT_HIGH);
    if (IS_ERR(priv->pd_gpio)) {
        priv->pd_gpio = 0;
        DBGOUT("%s: mute not used\n", __func__);
    }
    else {
        if (of_property_read_bool(dev->of_node, "inversion-mute"))
            priv->inverse_mute = true;
        else
            priv->inverse_mute = false;
        gpiod_set_value(priv->pd_gpio, priv->inverse_mute ? 0 : 1);
        DBGOUT("%s: mute used, inverse flag = %d\n", __func__, (int)priv->inverse_mute);
    }

    return snd_soc_register_component(dev, &soc_codec_i2s_clock_board,
      &i2s_clock_board_dai, 1);
}

static void i2s_clock_board_remove(struct platform_device *pdev)
{
    snd_soc_unregister_component(&pdev->dev);
}

static struct platform_driver i2s_clock_board_driver = {
    .probe              = i2s_clock_board_probe,
    .remove             = i2s_clock_board_remove,
    .driver             = {
        .name           = "i2s_clock_board",
        .of_match_table = of_match_ptr(i2s_clock_board_dt_ids),
    },
};

module_platform_driver(i2s_clock_board_driver);

MODULE_DESCRIPTION("ASoC external clock board for I2S bus");
MODULE_LICENSE("GPL v2");